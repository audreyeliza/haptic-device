"""Desktop launcher for the haptic-device CHAI3D simulation.

Lets you pick launch parameters (haptic mode, atom count/structure file,
potential energy surface, PBC), start/stop the compiled haptic-device binary,
and tweak a handful of parameters live while it runs via the loopback IPC
command server built into LJ.cpp (see ipcServer.cpp/.h).

Run with:  python -m launcher.main
"""
# COMMANDS ON HOW TO RUN THIS THING- run one by one ----------------------
# cd .../chai3d/haptic-device/launcher
# python3 -m venv .venv && source .venv/bin/activate
# pip install -r requirements.txt
# cd .../chai3d/haptic-device
# python -m launcher.main

# SKYLAR HERE ARE ALL THE IMPORTS --------------------------


"""
import sys

from PySide6.QtCore import QProcess, QProcessEnvironment, QTimer, Qt
from PySide6.QtWidgets import (
    QApplication,
    QCheckBox,
    QComboBox,
    QFileDialog,
    QFormLayout,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QMainWindow,
    QMessageBox,
    QPlainTextEdit,
    QPushButton,
    QRadioButton,
    QSpinBox,
    QVBoxLayout,
    QWidget,
)

from launcher.ipc_client import IpcClient
from launcher.paths import default_binary_path
launcher/ipc_client.py


from collections import deque

from PySide6.QtCore import QObject, Signal
from PySide6.QtNetwork import QAbstractSocket, QTcpSocket
launcher/paths.py


import platform
import struct
from pathlib import Path
"""




import sys

from PySide6.QtCore import QProcess, QProcessEnvironment, QTimer, Qt
from PySide6.QtWidgets import (
    QApplication,
    QCheckBox,
    QComboBox,
    QDoubleSpinBox,
    QFileDialog,
    QFormLayout,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QMainWindow,
    QMessageBox,
    QPlainTextEdit,
    QPushButton,
    QRadioButton,
    QScrollArea,
    QSlider,
    QSpinBox,
    QVBoxLayout,
    QWidget,
)

from launcher.ipc_client import IpcClient
from launcher.paths import default_binary_path

IPC_CONNECT_RETRY_MS = 400
IPC_CONNECT_MAX_ATTEMPTS = 25  # ~10 seconds before giving up
STATUS_POLL_MS = 500

# must match MIN/MAX_SIMULATION_TIME_STEP in globals.h
MIN_TIME_STEP_S = 1.0
MAX_TIME_STEP_S = 30.0

# must match the MIN_/MAX_ bounds for these in globals.h
MIN_SETTLING_ERROR = 0.001
MAX_SETTLING_ERROR = 1.0
MIN_K_RETURN = 0.0
MAX_K_RETURN = 500.0
MIN_K_DAMPEN = 0.0
MAX_K_DAMPEN = 50.0
MIN_RETURN_DELAY_S = 0.0
MAX_RETURN_DELAY_S = 30.0

# must match MIN_/MAX_FORCE_SCALE in globals.h
MIN_FORCE_SCALE = 0.0
MAX_FORCE_SCALE = 1.0


class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("Haptic Device Launcher")
        self.resize(780, 900)

        self.process = QProcess(self)
        self.process.readyReadStandardOutput.connect(self._on_stdout)
        self.process.readyReadStandardError.connect(self._on_stderr)
        self.process.finished.connect(self._on_process_finished)
        self.process.errorOccurred.connect(self._on_process_error)

        self.ipc = IpcClient(self)
        self.ipc.connected.connect(self._on_ipc_connected)
        self.ipc.disconnected.connect(self._on_ipc_disconnected)
        self.ipc.statusReceived.connect(self._on_status)
        self.ipc.commandFailed.connect(self._on_command_failed)

        self._connect_attempts = 0
        self._connect_timer = QTimer(self)
        self._connect_timer.setInterval(IPC_CONNECT_RETRY_MS)
        self._connect_timer.timeout.connect(self._try_connect_ipc)

        self._status_timer = QTimer(self)
        self._status_timer.setInterval(STATUS_POLL_MS)
        self._status_timer.timeout.connect(lambda: self.ipc.send("status"))

        self._build_ui()
        self._set_running_state(False)

    # ------------------------------------------------------------------
    # UI construction
    # ------------------------------------------------------------------
    def _build_ui(self):
        content = QWidget()
        root = QVBoxLayout(content)
        root.setSpacing(14)
        root.setContentsMargins(10, 10, 10, 16)

        root.addWidget(self._build_launch_group())
        root.addWidget(self._build_log_group())
        root.addWidget(self._build_live_group())
        root.addWidget(self._build_debug_group())
        root.addWidget(self._build_haptic_tuning_group())

        # Wrapping in a scroll area means the window can start smaller than
        # its content (e.g. so it fits above a dock/taskbar) without ever
        # clipping the bottom controls — just scroll instead of resizing.
        scroll = QScrollArea()
        scroll.setWidget(content)
        scroll.setWidgetResizable(True)
        self.setCentralWidget(scroll)

    def _build_launch_group(self) -> QGroupBox:
        group = QGroupBox("Launch configuration")
        form = QFormLayout(group)

        binary_row = QHBoxLayout()
        self.binary_edit = QLineEdit(str(default_binary_path()))
        browse_binary = QPushButton("Browse...")
        browse_binary.clicked.connect(self._browse_binary)
        binary_row.addWidget(self.binary_edit)
        binary_row.addWidget(browse_binary)
        form.addRow("haptic-device binary:", binary_row)

        self.mode_combo = QComboBox()
        self.mode_combo.addItems(["position", "force", "standby"])
        form.addRow("Haptic mode:", self.mode_combo)

        atom_source_row = QHBoxLayout()
        self.atom_count_radio = QRadioButton("Atom count")
        self.atom_count_radio.setChecked(True)
        self.structure_file_radio = QRadioButton("Structure/config file")
        atom_source_row.addWidget(self.atom_count_radio)
        atom_source_row.addWidget(self.structure_file_radio)
        form.addRow("Atom source:", atom_source_row)

        self.atom_count_spin = QSpinBox()
        self.atom_count_spin.setRange(1, 500)
        self.atom_count_spin.setValue(5)
        form.addRow("  Number of atoms:", self.atom_count_spin)

        file_row = QHBoxLayout()
        self.structure_file_edit = QLineEdit()
        self.structure_file_edit.setPlaceholderText(
            "e.g. example.con, POSCAR, or structure.xyz (see ../bin/resources/data)"
        )
        browse_structure = QPushButton("Browse...")
        browse_structure.clicked.connect(self._browse_structure_file)
        file_row.addWidget(self.structure_file_edit)
        file_row.addWidget(browse_structure)
        form.addRow("  File path:", file_row)

        self.atom_count_radio.toggled.connect(self._update_atom_source_enabled)
        self._update_atom_source_enabled()

        self.potential_combo = QComboBox()
        self.potential_combo.addItems(["lj", "morse", "ase"])
        self.potential_combo.currentTextChanged.connect(self._update_ase_enabled)
        form.addRow("Potential:", self.potential_combo)

        self.ase_spec_combo = QComboBox()
        self.ase_spec_combo.setEditable(True)
        self.ase_spec_combo.addItems(
            ["", "lj", "morse", "emt", "uma", "uma:omol", "uma:omat", "uma:oc20"]
        )
        self.ase_spec_combo.lineEdit().setPlaceholderText(
            "pick a preset, or type module:Class[:kwargs] (blank = default ASE calculator)"
        )
        self.ase_spec_combo.setEnabled(False)
        form.addRow("  ASE calculator spec:", self.ase_spec_combo)

        self.pbc_combo = QComboBox()
        self.pbc_combo.addItem("Keep from file", "keep")
        self.pbc_combo.addItem("Force PBC on", "on")
        self.pbc_combo.addItem("Force PBC off", "off")
        form.addRow("Periodic boundaries:", self.pbc_combo)

        intensity_row = QHBoxLayout()
        self.intensity_slider = QSlider(Qt.Orientation.Horizontal)
        self.intensity_slider.setRange(0, 100)
        self.intensity_slider.setValue(100)
        self.intensity_slider.setSingleStep(5)
        self.intensity_slider.setToolTip(
            "Scales all force feedback sent to the physical device. Lower this "
            "if your haptic device is old or worn to reduce strain on it; a "
            "hard safety cap is still applied on top of this at every setting."
        )
        self.intensity_value_label = QLabel("100%")
        self.intensity_slider.valueChanged.connect(
            lambda value: self.intensity_value_label.setText(f"{value}%")
        )
        intensity_row.addWidget(self.intensity_slider)
        intensity_row.addWidget(self.intensity_value_label)
        form.addRow("Feedback intensity:", intensity_row)

        self.time_step_spin = QDoubleSpinBox()
        self.time_step_spin.setDecimals(4)
        self.time_step_spin.setRange(MIN_TIME_STEP_S, MAX_TIME_STEP_S)
        self.time_step_spin.setSingleStep(0.0001)
        self.time_step_spin.setValue(1)
        self.time_step_spin.setSuffix(" fs")
        form.addRow("Initial time step:", self.time_step_spin)

        self.port_spin = QSpinBox()
        self.port_spin.setRange(1024, 65535)
        self.port_spin.setValue(8765)
        form.addRow("IPC control port:", self.port_spin)

        button_row = QHBoxLayout()
        self.launch_button = QPushButton("Launch")
        self.launch_button.clicked.connect(self._launch)
        self.stop_button = QPushButton("Stop")
        self.stop_button.clicked.connect(self._stop)
        button_row.addWidget(self.launch_button)
        button_row.addWidget(self.stop_button)
        form.addRow("", button_row)

        return group

    def _build_log_group(self) -> QGroupBox:
        group = QGroupBox("Simulation output")
        layout = QVBoxLayout(group)
        self.log_view = QPlainTextEdit()
        self.log_view.setReadOnly(True)
        self.log_view.setMaximumBlockCount(2000)
        layout.addWidget(self.log_view)
        return group

    def _build_live_group(self) -> QGroupBox:
        group = QGroupBox("Live controls (while running)")
        self.live_group = group
        layout = QVBoxLayout(group)
        layout.setSpacing(10)
        layout.setContentsMargins(10, 12, 10, 14)

        self.status_label = QLabel("Not connected.")
        layout.addWidget(self.status_label)

        row1 = QHBoxLayout()
        self.freeze_checkbox = QCheckBox("Freeze simulation")
        self.freeze_checkbox.toggled.connect(
            lambda checked: self.ipc.send(f"set freeze {'true' if checked else 'false'}")
        )
        row1.addWidget(self.freeze_checkbox)

        row1.addWidget(QLabel("Haptic mode:"))
        self.live_mode_combo = QComboBox()
        self.live_mode_combo.addItems(["position", "force", "standby"])
        apply_mode = QPushButton("Apply")
        apply_mode.clicked.connect(
            lambda: self.ipc.send(f"set mode {self.live_mode_combo.currentText()}")
        )
        row1.addWidget(self.live_mode_combo)
        row1.addWidget(apply_mode)
        layout.addLayout(row1)

        row2 = QHBoxLayout()
        row2.addWidget(QLabel("Potential (lj/morse only):"))
        self.live_potential_combo = QComboBox()
        self.live_potential_combo.addItems(["lj", "morse"])
        apply_potential = QPushButton("Apply")
        apply_potential.clicked.connect(
            lambda: self.ipc.send(f"set potential {self.live_potential_combo.currentText()}")
        )
        row2.addWidget(self.live_potential_combo)
        row2.addWidget(apply_potential)
        layout.addLayout(row2)

        row3 = QHBoxLayout()
        anchor_all = QPushButton("Anchor all")
        anchor_all.clicked.connect(lambda: self.ipc.send("anchor_all"))
        unanchor_all = QPushButton("Unanchor all")
        unanchor_all.clicked.connect(lambda: self.ipc.send("unanchor_all"))
        next_atom = QPushButton("Next atom")
        next_atom.clicked.connect(lambda: self.ipc.send("next_atom"))
        next_camera = QPushButton("Next camera")
        next_camera.clicked.connect(lambda: self.ipc.send("next_camera"))
        for button in (anchor_all, unanchor_all, next_atom, next_camera):
            row3.addWidget(button)
        layout.addLayout(row3)

        return group

    def _build_debug_group(self) -> QGroupBox:
        group = QGroupBox("Debug rendering")
        self.debug_group = group
        layout = QHBoxLayout(group)

        self.render_atoms_checkbox = QCheckBox("Atoms")
        self.render_atoms_checkbox.setChecked(True)
        self.render_atoms_checkbox.toggled.connect(
            lambda checked: self.ipc.send(f"set render_atoms {'true' if checked else 'false'}")
        )
        layout.addWidget(self.render_atoms_checkbox)

        self.render_forces_checkbox = QCheckBox("Force vectors")
        self.render_forces_checkbox.setChecked(True)
        self.render_forces_checkbox.toggled.connect(
            lambda checked: self.ipc.send(f"set render_forces {'true' if checked else 'false'}")
        )
        layout.addWidget(self.render_forces_checkbox)

        self.render_bonds_checkbox = QCheckBox("Bonds")
        self.render_bonds_checkbox.setChecked(True)
        self.render_bonds_checkbox.toggled.connect(
            lambda checked: self.ipc.send(f"set render_bonds {'true' if checked else 'false'}")
        )
        layout.addWidget(self.render_bonds_checkbox)

        return group

    def _build_haptic_tuning_group(self) -> QGroupBox:
        group = QGroupBox("Haptic feedback tuning")
        self.haptic_tuning_group = group
        form = QFormLayout(group)

        self.force_scale_spin = QDoubleSpinBox()
        self.force_scale_spin.setDecimals(2)
        self.force_scale_spin.setRange(MIN_FORCE_SCALE, MAX_FORCE_SCALE)
        self.force_scale_spin.setSingleStep(0.05)
        self.force_scale_spin.setValue(1.0)
        self.force_scale_spin.setToolTip(
            "Scales all force feedback sent to the device. Turn down while "
            "running if an old/worn device is feeling too strong."
        )
        form.addRow("Feedback intensity:", self.force_scale_spin)

        self.settling_err_spin = QDoubleSpinBox()
        self.settling_err_spin.setDecimals(4)
        self.settling_err_spin.setRange(MIN_SETTLING_ERROR, MAX_SETTLING_ERROR)
        self.settling_err_spin.setSingleStep(0.001)
        self.settling_err_spin.setValue(0.05)
        form.addRow("Settling error:", self.settling_err_spin)

        self.k_return_spin = QDoubleSpinBox()
        self.k_return_spin.setDecimals(2)
        self.k_return_spin.setRange(MIN_K_RETURN, MAX_K_RETURN)
        self.k_return_spin.setSingleStep(1.0)
        self.k_return_spin.setValue(25.0)
        form.addRow("K return:", self.k_return_spin)

        self.k_dampen_spin = QDoubleSpinBox()
        self.k_dampen_spin.setDecimals(2)
        self.k_dampen_spin.setRange(MIN_K_DAMPEN, MAX_K_DAMPEN)
        self.k_dampen_spin.setSingleStep(0.5)
        self.k_dampen_spin.setValue(0.0)
        form.addRow("K dampen:", self.k_dampen_spin)

        self.return_delay_spin = QDoubleSpinBox()
        self.return_delay_spin.setDecimals(2)
        self.return_delay_spin.setRange(MIN_RETURN_DELAY_S, MAX_RETURN_DELAY_S)
        self.return_delay_spin.setSingleStep(0.1)
        self.return_delay_spin.setValue(2.5)
        self.return_delay_spin.setSuffix(" s")
        form.addRow("Return delay:", self.return_delay_spin)

        apply_button = QPushButton("Apply")
        apply_button.clicked.connect(self._apply_haptic_tuning)
        form.addRow("", apply_button)

        return group

    def _apply_haptic_tuning(self):
        self.ipc.send(f"set force_scale {self.force_scale_spin.value():.2f}")
        self.ipc.send(f"set settling_err {self.settling_err_spin.value():.4f}")
        self.ipc.send(f"set k_return {self.k_return_spin.value():.2f}")
        self.ipc.send(f"set k_dampen {self.k_dampen_spin.value():.2f}")
        self.ipc.send(f"set return_delay {self.return_delay_spin.value():.2f}")

    def _update_atom_source_enabled(self):
        use_count = self.atom_count_radio.isChecked()
        self.atom_count_spin.setEnabled(use_count)
        self.structure_file_edit.setEnabled(not use_count)

    def _update_ase_enabled(self, potential: str):
        self.ase_spec_combo.setEnabled(potential == "ase")

    def _browse_binary(self):
        path, _ = QFileDialog.getOpenFileName(self, "Select haptic-device binary")
        if path:
            self.binary_edit.setText(path)

    def _browse_structure_file(self):
        path, _ = QFileDialog.getOpenFileName(
            self,
            "Select structure/config file",
            "",
            "ASE/config structures (*.con *.xyz *.cif *.vasp POSCAR CONTCAR);;All files (*)",
        )
        if path:
            self.structure_file_edit.setText(path)
            self.structure_file_radio.setChecked(True)

    # ------------------------------------------------------------------
    # Launching / stopping the simulation process
    # ------------------------------------------------------------------
    def _build_arguments(self) -> list[str]:
        # positions mirror LJ.cpp's argv[1..5]: mode, atoms/file, potential,
        # ASE spec, PBC mode. Always passing all five keeps this in lock-step
        # with the C++ side instead of relying on conditional placeholders.
        args = [self.mode_combo.currentText()]

        if self.structure_file_radio.isChecked():
            args.append(self.structure_file_edit.text().strip())
        else:
            args.append(str(self.atom_count_spin.value()))

        potential = self.potential_combo.currentText()
        args.append(potential)
        args.append(self.ase_spec_combo.currentText().strip())
        args.append(self.pbc_combo.currentData())

        return args

    def _launch(self):
        binary_path = self.binary_edit.text().strip()
        if not binary_path:
            QMessageBox.warning(self, "Missing binary", "Choose the haptic-device binary first.")
            return
        if self.structure_file_radio.isChecked() and not self.structure_file_edit.text().strip():
            QMessageBox.warning(self, "Missing file", "Choose a structure/config file, or switch to atom count.")
            return

        args = self._build_arguments()
        self.log_view.clear()
        self.log_view.appendPlainText(f"$ {binary_path} {' '.join(args)}")

        intensity = self.intensity_slider.value() / 100.0

        env = QProcessEnvironment.systemEnvironment()
        env.insert("HAPTIC_DEVICE_CMD_PORT", str(self.port_spin.value()))
        env.insert("HAPTIC_DEVICE_TIME_STEP", f"{self.time_step_spin.value():.4f}")
        env.insert("HAPTIC_DEVICE_FORCE_SCALE", f"{intensity:.2f}")
        self.process.setProcessEnvironment(env)

        self.force_scale_spin.blockSignals(True)
        self.force_scale_spin.setValue(intensity)
        self.force_scale_spin.blockSignals(False)

        self.process.setProgram(binary_path)
        self.process.setArguments(args)
        self.process.start()
        self._set_running_state(True)

        self._connect_attempts = 0
        self._connect_timer.start()

    def _stop(self):
        self._connect_timer.stop()
        self._status_timer.stop()
        self.ipc.disconnect_from_host()
        if self.process.state() != QProcess.ProcessState.NotRunning:
            self.process.terminate()
            if not self.process.waitForFinished(2000):
                self.process.kill()
        self._set_running_state(False)

    def _set_running_state(self, running: bool):
        self.launch_button.setEnabled(not running)
        self.stop_button.setEnabled(running)
        self.live_group.setEnabled(False)  # live controls need an active IPC connection too
        self.debug_group.setEnabled(False)
        self.haptic_tuning_group.setEnabled(False)
        if not running:
            self.status_label.setText("Not connected.")

    # ------------------------------------------------------------------
    # QProcess callbacks
    # ------------------------------------------------------------------
    def _on_stdout(self):
        data = bytes(self.process.readAllStandardOutput()).decode("utf-8", errors="replace")
        self.log_view.appendPlainText(data.rstrip("\n"))

    def _on_stderr(self):
        data = bytes(self.process.readAllStandardError()).decode("utf-8", errors="replace")
        self.log_view.appendPlainText(data.rstrip("\n"))

    def _on_process_finished(self, exit_code, exit_status):
        self.log_view.appendPlainText(f"[process exited, code={exit_code}]")
        self._connect_timer.stop()
        self._status_timer.stop()
        self.ipc.disconnect_from_host()
        self._set_running_state(False)

    def _on_process_error(self, error):
        self.log_view.appendPlainText(f"[process error: {self.process.errorString()}]")

    # ------------------------------------------------------------------
    # IPC callbacks
    # ------------------------------------------------------------------
    def _try_connect_ipc(self):
        if self.ipc.is_connected():
            self._connect_timer.stop()
            return
        self._connect_attempts += 1
        if self._connect_attempts > IPC_CONNECT_MAX_ATTEMPTS:
            self._connect_timer.stop()
            self.status_label.setText(
                "Could not reach the live-control port; live controls disabled for this run."
            )
            return
        self.ipc.connect_to("127.0.0.1", self.port_spin.value())

    def _on_ipc_connected(self):
        self._connect_timer.stop()
        self.live_group.setEnabled(True)
        self.debug_group.setEnabled(True)
        self.haptic_tuning_group.setEnabled(True)
        self.status_label.setText("Connected.")
        self._status_timer.start()
        self.ipc.send("status")

    def _on_ipc_disconnected(self):
        self._status_timer.stop()
        self.live_group.setEnabled(False)
        self.debug_group.setEnabled(False)
        self.haptic_tuning_group.setEnabled(False)
        self.status_label.setText("Live control disconnected.")

    def _on_status(self, fields: dict):
        self.status_label.setText(
            "mode={mode}  freeze={freeze}  potential={potential}  atoms={atoms}  "
            "anchored={anchored}  energy={energy}  timestep={timestep}".format(**fields)
        )
        self.freeze_checkbox.blockSignals(True)
        self.freeze_checkbox.setChecked(fields.get("freeze") == "true")
        self.freeze_checkbox.blockSignals(False)

        for checkbox, key in (
            (self.render_atoms_checkbox, "render_atoms"),
            (self.render_forces_checkbox, "render_forces"),
            (self.render_bonds_checkbox, "render_bonds"),
        ):
            if key in fields:
                checkbox.blockSignals(True)
                checkbox.setChecked(fields[key] == "true")
                checkbox.blockSignals(False)


        # don't fight the user while they're actively editing one of these
        for spin, key in (
            (self.force_scale_spin, "force_scale"),
            (self.settling_err_spin, "settling_err"),
            (self.k_return_spin, "k_return"),
            (self.k_dampen_spin, "k_dampen"),
            (self.return_delay_spin, "return_delay"),
        ):
            if key in fields and not spin.hasFocus():
                try:
                    value = float(fields[key])
                except ValueError:
                    continue
                spin.blockSignals(True)
                spin.setValue(value)
                spin.blockSignals(False)

    def _on_command_failed(self, command: str, message: str):
        self.log_view.appendPlainText(f"[live control] '{command}' -> {message}")

    def closeEvent(self, event):
        self._stop()
        super().closeEvent(event)


def main():
    app = QApplication(sys.argv)
    window = MainWindow()
    window.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
