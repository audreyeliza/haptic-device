// Computes interatomic forces and potential energies.
// The native Morse and LJ calculators run entirely in C++.
// The ASE calculator delegates to Python via the embedded interpreter,
// which allows using any ASE-compatible physics engine including ML potentials.

#include "potentials.h"

#include <Python.h>

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <vector>

#include "atom.h"

#include <filesystem>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

extern double centerCoords[3];

namespace
{

    // Haptic display coordinates are in meters; ASE expects angstroms.
    // This factor converts between the two when passing positions to Python
    // and when interpreting forces coming back.
    constexpr double kDistanceScale = 0.02;

    // These are module-level handles to the Python objects we reuse across frames.
    // They are initialized once in parseCalculatorSpec and kept alive for the
    // lifetime of the simulation.
    PyObject *calcObject = nullptr;   // the ASE calculator instance
    PyObject *aseModule = nullptr;    // the top-level ase module
    PyObject *atomsClass = nullptr;   // ase.Atoms constructor

    // The Atoms object is created on the first call to runAseCalculation,
    // then reused with updated positions on every subsequent frame.
    PyObject *atomsObject = nullptr;

    // Prints the Python traceback before exiting so the user can see what went wrong.
    [[noreturn]] void failWithPythonError(const char *message) {
        PyErr_Print();
        std::cerr << message << std::endl;
        std::exit(1);
    }

    // Only works on Linux/macOS: reads /proc/self/exe or resolves the executable
    // path to determine the executable's directory.
    std::string getExecutableDir();

    // Python is initialized lazily, only when the ASE calculator is first used.
    // We configure it to use the bundled virtual environment so ASE and its
    // dependencies are found correctly regardless of the system Python.
    void ensurePythonInitialized() {
        if (!Py_IsInitialized()) {
            PyConfig config;
            PyConfig_InitPythonConfig(&config);

            config.isolated = 0;
            config.use_environment = 1;

            // Point to the venv Python so sys.path picks up the right site-packages.
            // Resolved relative to the executable (not the current working
            // directory) so this works regardless of where the binary is launched
            // from, e.g. the launcher's own directory.
            std::filesystem::path venvPython;
            std::string executableDir = getExecutableDir();
            if (!executableDir.empty()) {
                venvPython = std::filesystem::path(executableDir) / ".." / ".." /
                             "haptic-device" / "uma_env" / "bin" / "python";
                // Normalize lexically only -- do NOT resolve symlinks here.
                // uma_env/bin/python is itself a symlink to the base
                // interpreter, and CPython locates the venv by finding
                // pyvenv.cfg next to that symlink; canonicalizing away the
                // symlink makes it look like the system Python instead.
                venvPython = venvPython.lexically_normal();
            } else {
                venvPython = "./haptic-device/uma_env/bin/python";
            }
            config.program_name = Py_DecodeLocale(venvPython.string().c_str(), NULL);

            config.module_search_paths_set = 0;

            PyStatus status = Py_InitializeFromConfig(&config);

            PyConfig_Clear(&config);

            if (PyStatus_Exception(status)) {
                std::cerr << "Python initialization failed\n";
                std::exit(1);
            }

            // Print diagnostic info at startup to confirm the right interpreter
            // and ASE installation are being used.
            PyRun_SimpleString(
                "import sys\n"
                "print('\\n=== EMBEDDED PYTHON DEBUG ===')\n"
                "print('Executable:', sys.executable)\n"
                "print('Prefix:', sys.prefix)\n"
                "print('Version:', sys.version)\n"
                "print('Path:')\n"
                "for p in sys.path:\n"
                "    print('  ', p)\n"
                "try:\n"
                "    import ase\n"
                "    print('ASE FOUND:', ase.__file__)\n"
                "except Exception as e:\n"
                "    print('ASE IMPORT FAILED:', e)\n"
                "print('=============================\\n')\n"
            );
            // Release the GIL so the haptic thread can run while Python is idle.
            PyEval_SaveThread();
        }
    }

    PyObject *importModule(const char *moduleName) {
        PyObject *module = PyImport_ImportModule(moduleName);
        if (module == nullptr) {
            failWithPythonError("Failed to import a required Python module.");
        }
        return module;
    }

    PyObject *getCallable(PyObject *module, const char *attributeName) {
        PyObject *callable = PyObject_GetAttrString(module, attributeName);
        if (callable == nullptr || !PyCallable_Check(callable)) {
            Py_XDECREF(callable);
            Py_DECREF(module);
            failWithPythonError("Failed to resolve a required Python callable.");
        }
        return callable;
    }

    // Converts atom positions from display space back to ASE coordinates
    // by reversing the distance scale and re-adding the world center offset.
    std::vector<double> flattenPositions(const std::vector<Atom *> &spheres) {
        std::vector<double> positions;
        positions.reserve(spheres.size() * 3);
        for (const Atom *sphere : spheres) {
            cVector3d pos = sphere->getLocalPos();
            positions.push_back(pos.x() / kDistanceScale + centerCoords[0]);
            positions.push_back(pos.y() / kDistanceScale + centerCoords[1]);
            positions.push_back(pos.z() / kDistanceScale + centerCoords[2]);
        }
        return positions;
    }

    std::vector<int> collectAtomicNumbers(const std::vector<Atom *> &spheres) {
        std::vector<int> numbers;
        numbers.reserve(spheres.size());
        for (const Atom *sphere : spheres) {
            numbers.push_back(sphere->getAtomicNumber());
        }
        return numbers;
    }

    PyObject *callMethodNoArgs(PyObject *object, const char *methodName, const char *errorMessage) {
        PyObject *result = PyObject_CallMethod(object, methodName, nullptr);
        if (result == nullptr) {
            failWithPythonError(errorMessage);
        }
        return result;
    }

    PyObject *sequenceFast(PyObject *object, const char *errorMessage) {
        PyObject *sequence = PySequence_Fast(object, errorMessage);
        if (sequence == nullptr) {
            failWithPythonError(errorMessage);
        }
        return sequence;
    }

    PyObject *buildNumbersList(const std::vector<int> &atomicNumbers) {
        PyObject *numbers = PyList_New(atomicNumbers.size());
        if (numbers == nullptr) {
            failWithPythonError("Failed to allocate Python list for atomic numbers.");
        }
        for (Py_ssize_t index = 0; index < static_cast<Py_ssize_t>(atomicNumbers.size()); ++index) {
            PyObject *value = PyLong_FromLong(atomicNumbers[index]);
            if (value == nullptr) {
                Py_DECREF(numbers);
                failWithPythonError("Failed to convert atomic number for Python.");
            }
            PyList_SetItem(numbers, index, value);
        }
        return numbers;
    }

    // Positions are passed as a flat C++ vector, but ASE expects an Nx3 nested list.
    // This reshapes the data before handing it to Python.
    PyObject *buildPositionsList(const std::vector<double> &positions) {
        PyObject *rows = PyList_New(positions.size() / 3);
        if (rows == nullptr) {
            failWithPythonError("Failed to allocate Python list for positions.");
        }
        for (Py_ssize_t atomIndex = 0; atomIndex < static_cast<Py_ssize_t>(positions.size() / 3);
             ++atomIndex) {
            PyObject *row = PyList_New(3);
            if (row == nullptr) {
                Py_DECREF(rows);
                failWithPythonError("Failed to allocate Python position row.");
            }
            for (Py_ssize_t coordIndex = 0; coordIndex < 3; ++coordIndex) {
                PyObject *value =
                    PyFloat_FromDouble(positions[static_cast<size_t>(atomIndex * 3 + coordIndex)]);
                if (value == nullptr) {
                    Py_DECREF(row);
                    Py_DECREF(rows);
                    failWithPythonError("Failed to convert position value for Python.");
                }
                PyList_SetItem(row, coordIndex, value);
            }
            PyList_SetItem(rows, atomIndex, row);
        }
        return rows;
    }

    // The cell is a 3x3 matrix of lattice vectors that defines the periodic simulation box.
    PyObject *buildCellList(const std::array<double, 9> &cellMatrix) {
        PyObject *cell = PyList_New(3);
        if (cell == nullptr) {
            failWithPythonError("Failed to allocate Python list for cell.");
        }

        for (Py_ssize_t rowIndex = 0; rowIndex < 3; ++rowIndex) {
            PyObject *row = PyList_New(3);
            if (row == nullptr) {
                Py_DECREF(cell);
                failWithPythonError("Failed to allocate Python cell row.");
            }

            for (Py_ssize_t columnIndex = 0; columnIndex < 3; ++columnIndex) {
                const size_t flatIndex = static_cast<size_t>(rowIndex * 3 + columnIndex);
                PyObject *value = PyFloat_FromDouble(cellMatrix[flatIndex]);
                if (value == nullptr) {
                    Py_DECREF(row);
                    Py_DECREF(cell);
                    failWithPythonError("Failed to convert cell value for Python.");
                }
                PyList_SetItem(row, columnIndex, value);
            }

            PyList_SetItem(cell, rowIndex, row);
        }
        return cell;
    }

    // Periodic boundary conditions: one boolean per axis.
    // When true, atoms that leave one side of the box reenter from the opposite side.
    PyObject *buildPbcList(const std::array<int, 3> &periodicBoundaryConditions) {
        PyObject *pbc = PyList_New(3);
        if (pbc == nullptr) {
            failWithPythonError("Failed to allocate Python list for PBC.");
        }

        for (Py_ssize_t index = 0; index < 3; ++index) {
            PyObject *value = PyBool_FromLong(periodicBoundaryConditions[static_cast<size_t>(index)]);
            if (value == nullptr) {
                Py_DECREF(pbc);
                failWithPythonError("Failed to convert PBC value for Python.");
            }
            PyList_SetItem(pbc, index, value);
        }

        return pbc;
    }

    // The kwargs string is user-supplied configuration for the calculator (e.g. model name,
    // cutoff radius). We use ast.literal_eval rather than eval for safety: it only
    // accepts Python literals, not arbitrary code.
    PyObject *buildKwargsDict(const std::string &kwargsText) {
        if (kwargsText.empty()) {
            return PyDict_New();
        }

        PyObject *astModule = importModule("ast");
        PyObject *literalEval = getCallable(astModule, "literal_eval");
        PyObject *kwargsString = PyUnicode_FromString(kwargsText.c_str());
        if (kwargsString == nullptr) {
            Py_DECREF(literalEval);
            Py_DECREF(astModule);
            failWithPythonError("Failed to convert ASE calculator kwargs for Python.");
        }
        PyObject *parsedKwargs = PyObject_CallFunctionObjArgs(literalEval, kwargsString, nullptr);
        Py_DECREF(kwargsString);
        Py_DECREF(literalEval);
        Py_DECREF(astModule);
        if (parsedKwargs == nullptr) {
            failWithPythonError("Failed to parse ASE calculator kwargs.");
        }
        if (!PyDict_Check(parsedKwargs)) {
            Py_DECREF(parsedKwargs);
            std::cerr << "ASE calculator kwargs must evaluate to a dict." << std::endl;
            std::exit(1);
        }
        return parsedKwargs;
    }

    // Determines the executable's directory so Python/module paths can be
    // resolved regardless of the process's current working directory.
    std::string getExecutableDir()
    {
        char buffer[4096];
#if defined(__APPLE__)
        uint32_t size = sizeof(buffer);
        if (_NSGetExecutablePath(buffer, &size) != 0)
        {
            return "";
        }
        std::string executablePath(buffer);
#else
        ssize_t length = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
        if (length <= 0)
        {
            return "";
        }
        buffer[length] = '\0';
        std::string executablePath(buffer);
#endif
        size_t separator = executablePath.find_last_of('/');
        if (separator == std::string::npos)
        {
            return "";
        }

        return executablePath.substr(0, separator);
    }

    // Resolves a short spec string to the Python module, class, and kwargs needed
    // to construct an ASE calculator. Accepts built-in aliases for common potentials
    // or a generic "module:Class[:kwargs]" format for anything else.
    // After resolving, initializes Python and builds the calculator object.
    void parseCalculatorSpec(const std::string &spec,
                             std::string &moduleName,
                             std::string &className,
                             std::string &kwargsText) {
        if (spec.empty() || spec == "lj" || spec == "lennard-jones") {
            moduleName = "ase.calculators.lj";
            className = "LennardJones";
            kwargsText.clear();
        } else if (spec == "morse") {
            moduleName = "ase.calculators.morse";
            className = "MorsePotential";
            kwargsText.clear();
        } else if (spec == "emt") {
            // Effective Medium Theory, a fast empirical potential for metals.
            moduleName = "ase.calculators.emt";
            className = "EMT";
            kwargsText.clear();
        } else if (spec.rfind("uma", 0) == 0) {
            // UMA is Meta's universal ML potential. Its constructor takes the full
            // spec string directly, so we pass it through as kwargs.
            moduleName = "calculator";
            className = "create_calculator";
            kwargsText = spec;
        } else {
            // Generic format: "module:ClassName" or "module:ClassName:{...kwargs...}"
            size_t firstColon = spec.find(':');
            if (firstColon == std::string::npos) {
                std::cerr << "ASE calculator spec must be empty, a known alias "
                            "(`lj`, `morse`, `emt`, `uma`, `uma-remote`), or `module:Class[:kwargs]`."
                        << std::endl;
                std::exit(1);
            }
            moduleName = spec.substr(0, firstColon);
            size_t secondColon = spec.find(':', firstColon + 1);
            if (secondColon == std::string::npos) {
                className = spec.substr(firstColon + 1);
                kwargsText.clear();
            } else {
                className = spec.substr(firstColon + 1, secondColon - firstColon - 1);
                kwargsText = spec.substr(secondColon + 1);
            }
        }

        ensurePythonInitialized();

        // Acquire the GIL, required whenever we call into the Python C API.
        PyGILState_STATE gilState = PyGILState_Ensure();

        // Resolve the script directory relative to the executable, not the current
        // working directory. This makes module loading reliable when the binary is
        // launched from any folder.
        std::filesystem::path scriptDir = getExecutableDir();
        if (!scriptDir.empty()) {
            scriptDir = scriptDir / ".." / ".." / "haptic-device";
            scriptDir = std::filesystem::weakly_canonical(scriptDir);
        } else {
            scriptDir = std::filesystem::path("../haptic-device");
        }

        // Append our script directory to sys.path so custom modules (e.g. uma_wrapper) are found.
        PyObject *sysPath = PySys_GetObject("path");
        PyObject *pathStr = PyUnicode_FromString(scriptDir.string().c_str());
        PyList_Append(sysPath, pathStr);
        Py_DECREF(pathStr);

        // Load ASE and grab the Atoms class now so we don't repeat this on every frame.
        aseModule = importModule(spec == "uma-remote" ? "calculator" : "ase");
        atomsClass = getCallable(aseModule, "Atoms");

        PyObject *calcArgs = PyTuple_New(0);
        PyObject *calcKwargs = nullptr;

        if (moduleName == "calculator" && className == "create_calculator") {
            // UMA's factory function handles its own construction from the spec string.
            PyObject *wrapperModule = importModule("calculator");
            PyObject *resolver = getCallable(wrapperModule, "create_calculator");

            PyObject *specObj = PyUnicode_FromString(kwargsText.c_str());

            calcObject = PyObject_CallFunctionObjArgs(resolver, specObj, nullptr);

            Py_DECREF(specObj);

            if (!calcObject) {
                failWithPythonError("Failed to construct UMA calculator.");
            }
        } else {
            // Standard path: import the module, instantiate the class with optional kwargs.
            PyObject *calcModule = importModule(moduleName.c_str());
            PyObject *calcClass = getCallable(calcModule, className.c_str());

            calcKwargs = buildKwargsDict(kwargsText);

            calcObject = PyObject_Call(calcClass, calcArgs, calcKwargs);

            Py_DECREF(calcKwargs);
            calcKwargs = nullptr;
            Py_DECREF(calcClass);
            Py_DECREF(calcModule);

            if (!calcObject) {
                failWithPythonError("Failed to construct ASE calculator.");
            }
        }

        if (calcKwargs) {
            Py_DECREF(calcKwargs);
        }
        Py_DECREF(calcArgs);
        PyGILState_Release(gilState);
    }

    // Builds the ASE Atoms object from the current atom configuration and attaches
    // the calculator to it. This only runs once; on subsequent frames we update
    // positions in-place rather than rebuilding the whole object.
    PyObject* initializeCalculator(const std::vector<Atom *> &spheres,
                                   const std::array<double, 9> &cellMatrix,
                                   const std::array<int, 3> &periodicBoundaryConditions) {
        PyGILState_STATE gilState = PyGILState_Ensure();

        PyObject *atomsArgs = PyTuple_New(0);
        PyObject *atomsKwargs = PyDict_New();
        if (atomsArgs == nullptr || atomsKwargs == nullptr) {
            Py_XDECREF(atomsArgs);
            Py_XDECREF(atomsKwargs);
            failWithPythonError("Failed to allocate ASE Atoms constructor arguments.");
        }

        // Build each piece of the Atoms constructor call separately,
        // then pack them into the kwargs dict.
        PyObject *numbersObject = buildNumbersList(collectAtomicNumbers(spheres));
        PyObject *cellObject = buildCellList(cellMatrix);
        PyObject *pbcObject = buildPbcList(periodicBoundaryConditions);
        PyObject *positionsObject = buildPositionsList(flattenPositions(spheres));

        PyDict_SetItemString(atomsKwargs, "numbers", numbersObject);
        PyDict_SetItemString(atomsKwargs, "cell", cellObject);
        PyDict_SetItemString(atomsKwargs, "pbc", pbcObject);
        PyDict_SetItemString(atomsKwargs, "positions", positionsObject);

        // Some ML calculators (including UMA) require charge and spin in the info dict.
        PyObject *info = PyDict_New();
        PyDict_SetItemString(info, "charge", PyLong_FromLong(0));
        PyDict_SetItemString(info, "spin", PyLong_FromLong(1));
        PyDict_SetItemString(atomsKwargs, "info", info);

        Py_DECREF(info);
        Py_DECREF(numbersObject);
        Py_DECREF(cellObject);
        Py_DECREF(pbcObject);
        Py_DECREF(positionsObject);

        PyObject *atomsObject = PyObject_Call(atomsClass, atomsArgs, atomsKwargs);

        Py_DECREF(atomsKwargs);
        Py_DECREF(atomsArgs);

        if (atomsObject == nullptr) {
            failWithPythonError("Failed to construct ASE Atoms.");
        }
        // Attaching the calculator here means ASE will use it automatically
        // whenever get_forces() or get_potential_energy() is called.
        if (PyObject_SetAttrString(atomsObject, "calc", calcObject) != 0) {
            Py_DECREF(atomsObject);
            failWithPythonError("Failed to attach the ASE calculator to Atoms.");
        }

        return atomsObject;
    }


    // Runs an ASE single-point calculation and returns the results as a C++ vector.
    // The return format is: one [fx, fy, fz] entry per atom, followed by a single
    // entry holding the total potential energy.
    //
    // On the first call, the Atoms object is created. On every subsequent call,
    // only positions are updated; rebuilding Atoms each frame would be too slow
    // and would also reset internal calculator state (e.g. neighbor lists).
    std::vector<std::vector<double>> runAseCalculation(const std::vector<Atom *> &spheres,
                                                       const std::string &moduleName,
                                                       const std::string &className,
                                                       const std::string &kwargsText,
                                                       const std::array<double, 9> &cellMatrix,
                                                       const std::array<int, 3> &periodicBoundaryConditions)
    {
        PyGILState_STATE gilState = PyGILState_Ensure();

        if (atomsObject == nullptr) {
            atomsObject = initializeCalculator(spheres, cellMatrix, periodicBoundaryConditions);
        }

        // Push the latest atom positions into the existing Atoms object.
        PyObject *positionsObject = buildPositionsList(flattenPositions(spheres));
        PyObject* result = PyObject_CallMethod(atomsObject, "set_positions", "O", positionsObject);
        Py_XDECREF(result);

        // get_forces() triggers the full energy/force evaluation inside ASE.
        // We call get_potential_energy() separately rather than extracting it from
        // the forces result because ASE caches it after the first call, no extra cost.
        PyObject *forcesObject = PyObject_CallMethod(atomsObject, "get_forces", nullptr);
        if (forcesObject == nullptr) {
            Py_DECREF(atomsObject);
            failWithPythonError("Failed to evaluate ASE forces.");
        }

        PyObject *energyObject = PyObject_CallMethod(atomsObject, "get_potential_energy", nullptr);
        if (energyObject == nullptr)
        {
            Py_DECREF(forcesObject);
            failWithPythonError("Failed to evaluate ASE potential energy.");
        }

        // Convert the Nx3 force array from Python into a C++ vector of rows.
        PyObject *forceRows =
            PySequence_Fast(forcesObject, "ASE get_forces() result must be a sequence.");
        Py_DECREF(forcesObject);
        if (forceRows == nullptr)
        {
            Py_DECREF(energyObject);
            failWithPythonError("Failed to inspect ASE force rows.");
        }

        std::vector<std::vector<double>> returnVector;
        returnVector.reserve(spheres.size() + 1);
        PyObject **rowItems = PySequence_Fast_ITEMS(forceRows);
        for (Py_ssize_t atomIndex = 0; atomIndex < PySequence_Fast_GET_SIZE(forceRows); ++atomIndex)
        {
            PyObject *rowSequence =
                PySequence_Fast(rowItems[atomIndex], "Each ASE force row must be a sequence.");
            if (rowSequence == nullptr)
            {
                Py_DECREF(forceRows);
                Py_DECREF(energyObject);
                failWithPythonError("Failed to inspect ASE force components.");
            }

            PyObject **coordItems = PySequence_Fast_ITEMS(rowSequence);
            std::vector<double> pushBack = {PyFloat_AsDouble(coordItems[0]),
                                            PyFloat_AsDouble(coordItems[1]),
                                            PyFloat_AsDouble(coordItems[2])};
            returnVector.push_back(pushBack);
            Py_DECREF(rowSequence);
        }
        Py_DECREF(forceRows);

        // Append energy as the final element so callers can retrieve both forces
        // and energy from the same return value.
        returnVector.push_back({PyFloat_AsDouble(energyObject)});
        Py_DECREF(energyObject);
        PyGILState_Release(gilState);

        return returnVector;
    }

    std::array<double, 9> extractCellMatrix(PyObject *cellObject)
    {
        std::array<double, 9> cell = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
        PyObject *rows = sequenceFast(cellObject, "ASE cell must be a 3x3 sequence.");
        if (PySequence_Fast_GET_SIZE(rows) != 3)
        {
            Py_DECREF(rows);
            std::cerr << "ASE cell must contain exactly three cell vectors." << std::endl;
            std::exit(1);
        }

        for (Py_ssize_t rowIndex = 0; rowIndex < 3; ++rowIndex)
        {
            PyObject *row = sequenceFast(PySequence_Fast_GET_ITEM(rows, rowIndex),
                                         "ASE cell row must be a sequence of length 3.");
            if (PySequence_Fast_GET_SIZE(row) != 3)
            {
                Py_DECREF(row);
                Py_DECREF(rows);
                std::cerr << "Each ASE cell vector must have exactly three coordinates." << std::endl;
                std::exit(1);
            }

            for (Py_ssize_t columnIndex = 0; columnIndex < 3; ++columnIndex)
            {
                cell[static_cast<size_t>(rowIndex * 3 + columnIndex)] =
                    PyFloat_AsDouble(PySequence_Fast_GET_ITEM(row, columnIndex));
                if (PyErr_Occurred())
                {
                    Py_DECREF(row);
                    Py_DECREF(rows);
                    failWithPythonError("Failed to convert ASE cell value to C++.");
                }
            }
            Py_DECREF(row);
        }

        Py_DECREF(rows);
        return cell;
    }

    std::array<int, 3> extractPbc(PyObject *pbcObject)
    {
        std::array<int, 3> pbc = {0, 0, 0};
        PyObject *items = sequenceFast(pbcObject, "ASE pbc must be a sequence of length 3.");
        if (PySequence_Fast_GET_SIZE(items) != 3)
        {
            Py_DECREF(items);
            std::cerr << "ASE pbc must contain exactly three booleans." << std::endl;
            std::exit(1);
        }

        for (Py_ssize_t index = 0; index < 3; ++index)
        {
            pbc[static_cast<size_t>(index)] = PyObject_IsTrue(PySequence_Fast_GET_ITEM(items, index));
        }

        Py_DECREF(items);
        return pbc;
    }

    std::vector<int> extractAtomicNumbers(PyObject *numbersObject)
    {
        std::vector<int> atomicNumbers;
        PyObject *items = sequenceFast(numbersObject, "ASE atomic numbers must be a sequence.");
        atomicNumbers.reserve(static_cast<size_t>(PySequence_Fast_GET_SIZE(items)));

        for (Py_ssize_t index = 0; index < PySequence_Fast_GET_SIZE(items); ++index)
        {
            atomicNumbers.push_back(static_cast<int>(PyLong_AsLong(PySequence_Fast_GET_ITEM(items, index))));
            if (PyErr_Occurred())
            {
                Py_DECREF(items);
                failWithPythonError("Failed to convert ASE atomic number to C++.");
            }
        }

        Py_DECREF(items);
        return atomicNumbers;
    }

    std::vector<std::array<double, 3>> extractPositions(PyObject *positionsObject)
    {
        std::vector<std::array<double, 3>> positions;
        PyObject *rows = sequenceFast(positionsObject, "ASE positions must be an Nx3 sequence.");
        positions.reserve(static_cast<size_t>(PySequence_Fast_GET_SIZE(rows)));

        for (Py_ssize_t rowIndex = 0; rowIndex < PySequence_Fast_GET_SIZE(rows); ++rowIndex)
        {
            PyObject *row = sequenceFast(PySequence_Fast_GET_ITEM(rows, rowIndex),
                                         "ASE position row must be a sequence of length 3.");
            if (PySequence_Fast_GET_SIZE(row) != 3)
            {
                Py_DECREF(row);
                Py_DECREF(rows);
                std::cerr << "Each ASE position row must have exactly three coordinates." << std::endl;
                std::exit(1);
            }

            std::array<double, 3> position = {
                PyFloat_AsDouble(PySequence_Fast_GET_ITEM(row, 0)),
                PyFloat_AsDouble(PySequence_Fast_GET_ITEM(row, 1)),
                PyFloat_AsDouble(PySequence_Fast_GET_ITEM(row, 2))};
            if (PyErr_Occurred())
            {
                Py_DECREF(row);
                Py_DECREF(rows);
                failWithPythonError("Failed to convert ASE position to C++.");
            }
            positions.push_back(position);
            Py_DECREF(row);
        }

        Py_DECREF(rows);
        return positions;
    }

    

    // Single-quote escapes a string for safe use in a shell command.
    // An embedded single quote must be terminated, escaped, then reopened.
    std::string quoteForShell(const std::string &value)
    {
        std::string quoted = "'";
        for (char ch : value)
        {
            if (ch == '\'')
            {
                quoted += "'\\''";
            }
            else
            {
                quoted += ch;
            }
        }
        quoted += "'";
        return quoted;
    }

    std::vector<std::string> getAseFileIoCandidates() {
        std::vector<std::string> candidates;
        candidates.push_back("./haptic-device/ase_file_io.py");
        candidates.push_back("../haptic-device/ase_file_io.py");
        candidates.push_back("../../haptic-device/ase_file_io.py");
        return candidates;
    }

    std::string resolveAseFileIoScript()
    {
        for (const std::string &candidate : getAseFileIoCandidates())
        {
            std::ifstream script(candidate);
            if (script.good())
            {
                return candidate;
            }
        }

        // Build an informative error that lists every path we tried.
        std::ostringstream message;
        message << "Could not locate ase_file_io.py. Looked in:";
        for (const std::string &candidate : getAseFileIoCandidates())
        {
            message << "\n  " << candidate;
        }
        throw std::runtime_error(message.str());
    }

    // ASE is only guaranteed to be installed in the bundled uma_env venv (see
    // ensurePythonInitialized above), not on the system Python. Resolve that
    // same interpreter here so this subprocess can actually import ase.
    std::vector<std::string> getAsePythonCandidates() {
        std::vector<std::string> candidates;
        candidates.push_back("./haptic-device/uma_env/bin/python3");
        candidates.push_back("../haptic-device/uma_env/bin/python3");
        candidates.push_back("../../haptic-device/uma_env/bin/python3");
        return candidates;
    }

    std::string resolveAsePythonExecutable()
    {
        for (const std::string &candidate : getAsePythonCandidates())
        {
            std::ifstream interpreter(candidate);
            if (interpreter.good())
            {
                return candidate;
            }
        }

        // Fall back to whatever python3 is on PATH so this still works in
        // environments where ASE was installed system-wide instead.
        return "python3";
    }

}

// Loads an atom structure from a file (XYZ, CIF, etc.) by running ase_file_io.py
// as a subprocess. We use a subprocess rather than the embedded interpreter here
// because this only runs at load time and avoids having to manage ASE I/O state
// alongside the simulation state.
//
// The script prints: atom count, positions (Nx3), atomic numbers (N),
// cell matrix (3x3 flattened), and PBC flags (3 booleans).
AseStructureData loadAseStructure(const std::string &filename,
                                  const std::array<int, 3> &repeat)
{
    AseStructureData structure;
    const std::string scriptPath = resolveAseFileIoScript();
    const std::string pythonExecutable = resolveAsePythonExecutable();
    const std::string command =
        quoteForShell(pythonExecutable) + " " + quoteForShell(scriptPath) + " " + quoteForShell(filename)
        + " " + std::to_string(repeat[0])
        + " " + std::to_string(repeat[1])
        + " " + std::to_string(repeat[2]);
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(command.c_str(), "r"), pclose);
    if (!pipe) {
        throw std::runtime_error("Failed to start ASE structure loader helper.");
    }

    std::ostringstream output;
    char buffer[4096];
    while (fgets(buffer, sizeof(buffer), pipe.get()) != nullptr) {
        output << buffer;
    }

    const int status = pclose(pipe.release());
    if (status != 0) {
        throw std::runtime_error("ASE structure loader failed for \"" + filename + "\".");
    }

    // Parse the script's stdout sequentially; the format is fixed and documented
    // in ase_file_io.py.
    std::istringstream stream(output.str());
    int atomCount = 0;
    if (!(stream >> atomCount) || atomCount < 0) {
        throw std::runtime_error("ASE structure loader returned an invalid atom count.");
    }

    structure.positions.reserve(static_cast<size_t>(atomCount));
    for (int atomIndex = 0; atomIndex < atomCount; ++atomIndex)
    {
        std::array<double, 3> position = {0.0, 0.0, 0.0};
        if (!(stream >> position[0] >> position[1] >> position[2]))
        {
            throw std::runtime_error("ASE structure loader returned invalid positions.");
        }
        structure.positions.push_back(position);
    }

    structure.atomicNumbers.reserve(static_cast<size_t>(atomCount));
    for (int atomIndex = 0; atomIndex < atomCount; ++atomIndex)
    {
        int atomicNumber = 0;
        if (!(stream >> atomicNumber))
        {
            throw std::runtime_error("ASE structure loader returned invalid atomic numbers.");
        }
        structure.atomicNumbers.push_back(atomicNumber);
    }
    structure.radii.reserve(static_cast<size_t>(atomCount));

    for (int atomIndex = 0; atomIndex < atomCount; ++atomIndex)
    {
        double radius = 0.0;

        if (!(stream >> radius))
        {
            throw std::runtime_error("ASE structure loader returned invalid radii.");
        }

        structure.radii.push_back(radius);
    }

    for (size_t index = 0; index < structure.cell.size(); ++index)
    {
        if (!(stream >> structure.cell[index]))
        {
            throw std::runtime_error("ASE structure loader returned an invalid cell matrix.");
        }
    }

    for (size_t index = 0; index < structure.pbc.size(); ++index)
    {
        if (!(stream >> structure.pbc[index]))
        {
            throw std::runtime_error("ASE structure loader returned invalid PBC flags.");
        }
    }

    if (structure.positions.size() != structure.atomicNumbers.size())
    {
        throw std::runtime_error("ASE returned mismatched positions and atomic numbers.");
    }
    if (structure.positions.size() != structure.radii.size())
    {
        throw std::runtime_error("ASE returned mismatched positions and radii.");
    }

    return structure;
}

///////////////////////////// MORSE ///////////////////////////////////////
// The Morse potential models the bond between two atoms as an asymmetric well:
// a steep repulsive wall at short range and a gradual approach to zero at large distances.
// It's more physically accurate than LJ for bonds that can actually break.

// Returns per-atom force vectors and the total potential energy (appended as the last element).
vector<vector<double>> morseCalculator::getFandU(vector<Atom *> &spheres)
{
    vector<vector<double>> returnVector;
    double potentialEnergy = 0;
    Atom *current;
    for (int i = 0; i < spheres.size(); i++)
    {
        cVector3d force;
        current = spheres[i];
        cVector3d pos0 = current->getLocalPos();
        force.zero();

        // Sum contributions from every other atom.
        for (int j = 0; j < spheres.size(); j++)
        {
            if (i != j)
            {
                cVector3d pos1 = spheres[j]->getLocalPos();

                // Unit vector from j toward i, defines the direction of the force on atom i.
                cVector3d dir01 = cNormalize(pos0 - pos1);

                double distance = cDistance(pos0, pos1) / distanceScale;
                potentialEnergy += getMorseEnergy(distance);
                double appliedForce = getMorseForce(distance);
                force.add(appliedForce * dir01);
            }
        }
        vector<double> pushBack = {force.x(), force.y(), force.z()};
        returnVector.push_back(pushBack);
    }
    // Each pair (i,j) was counted twice, once from i's perspective and once from j's.
    // Dividing by 2 gives the true total energy.
    vector<double> potentE = {potentialEnergy / 2};
    returnVector.push_back(potentE);

    return returnVector;
}

// U(r) = epsilon * exp(rho0*(1 - r/r0)) * (exp(rho0*(1 - r/r0)) - 2)
// Energy is zero at large r, reaches minimum -epsilon at r = r0,
// and rises steeply as r approaches zero.
double morseCalculator::getMorseEnergy(double distance)
{
    double expf = exp(rho0 * (1.0 - distance / r0));
    return epsilon * expf * (expf - 2);
}

// F(r) = -dU/dr, projected onto the bond axis.
// forceDamping scales output to a range suitable for the haptic device.
double morseCalculator::getMorseForce(double distance)
{
    double temp = -2 * rho0 * epsilon * exp(rho0 - (2 * rho0 * distance) / r0) *
                  (exp((rho0 * distance) / r0) - exp(rho0));
    return temp / r0 * forceDamping;
}

//////////////////////////////// LJ ///////////////////////////////////////
// The Lennard-Jones potential uses a 12-6 form: the r^-12 term handles short-range
// repulsion and the r^-6 term handles the longer-range van der Waals attraction.
// It's fast to compute but less physical than Morse for covalent bonds.

// Returns per-atom force vectors and the total potential energy (appended as the last element).
vector<vector<double>> ljCalculator::getFandU(vector<Atom *> &spheres)
{
    vector<vector<double>> returnVector;
    double potentialEnergy = 0;
    Atom *current;
    for (int i = 0; i < spheres.size(); i++)
    {
        cVector3d force;
        current = spheres[i];
        cVector3d pos0 = current->getLocalPos();
        force.zero();

        for (int j = 0; j < spheres.size(); j++)
        {
            if (i != j)
            {
                cVector3d pos1 = spheres[j]->getLocalPos();

                cVector3d dir01 = cNormalize(pos0 - pos1);

                double distance = cDistance(pos0, pos1) / distanceScale;
                potentialEnergy += getLennardJonesEnergy(distance);
                double appliedForce = getLennardJonesForce(distance);
                force.add(appliedForce * dir01);
            }
        }
        vector<double> pushBack = {force.x(), force.y(), force.z()};
        returnVector.push_back(pushBack);
    }
    // Divide by 2 to correct for double-counting each pair.
    vector<double> potentE = {potentialEnergy / 2};
    returnVector.push_back(potentE);

    return returnVector;
}

// U(r) = 4*epsilon * [(sigma/r)^12 - (sigma/r)^6]
double ljCalculator::getLennardJonesEnergy(double distance)
{
    return 4 * epsilon * (pow(sigma / distance, 12) - pow(sigma / distance, 6));
}

// F(r) = -dU/dr along the bond axis.
double ljCalculator::getLennardJonesForce(double distance)
{
    return -4 * epsilon *
           ((-12 * pow(sigma / distance, 13)) - (-6 * pow(sigma / distance, 7)));
}

////////////////////////////////// ase //////////////////////////////////////
// The ASE calculator hands off force and energy evaluation to Python.
// This lets us use any ASE-compatible potential (classical, semi-empirical,
// or ML-based) without recompiling the C++ code.

aseCalculator::aseCalculator(const std::string &cName,
                             const std::array<double, 9> &cellMatrix,
                             const std::array<int, 3> &periodicBoundaryConditions)
    : cell(cellMatrix), pbc(periodicBoundaryConditions) {
    // parseCalculatorSpec initializes Python, loads the requested calculator module,
    // and stores a handle to the calculator object for use in getFandU.
    parseCalculatorSpec(cName, calculatorModule, calculatorClass, calculatorKwargs);
}

void aseCalculator::setTemperature(double temperatureValue) {
    temperature = temperatureValue;
    if (calcObject == nullptr) {
        return;
    }

    ensurePythonInitialized();

    PyGILState_STATE gilState = PyGILState_Ensure();
    PyObject *temperatureObject = PyFloat_FromDouble(temperature);
    if (temperatureObject == nullptr) {
        PyGILState_Release(gilState);
        failWithPythonError("Failed to convert ASE temperature value.");
    }
    int setResult = PyObject_SetAttrString(calcObject, "temperature", temperatureObject);

    Py_DECREF(temperatureObject);
    PyGILState_Release(gilState);
}

std::vector<std::vector<double>> aseCalculator::getFandU(std::vector<Atom *> &spheres) {
    return runAseCalculation(spheres, calculatorModule, calculatorClass, calculatorKwargs, cell, pbc);
}
