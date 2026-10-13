# Bridges a Windows COM port to a loopback TCP socket so WSL can reach the
# haptic device without usbipd (no admin needed). WSL side:
#   socat -d -d pty,raw,echo=0,link=$HOME/ttyBridge tcp:localhost:9001,nodelay
#
# Event-driven: both directions block on async reads and forward bytes the
# moment they arrive. The previous poll loop slept between checks, and
# Windows' default ~15.6 ms timer resolution turned a 2 ms sleep into ~15 ms
# of added latency per hop. NoDelay disables Nagle so the tiny 7/11-byte
# frames aren't held back waiting to be batched.
#
# Usage (Windows PowerShell):
#   powershell -ExecutionPolicy Bypass -File serial-bridge.ps1 -ComPort COM4
param(
  [string]$ComPort = "COM4",
  [int]$BaudRate = 115200,
  [int]$ListenPort = 9001
)

try {
    $serial = New-Object System.IO.Ports.SerialPort $ComPort, $BaudRate, ([System.IO.Ports.Parity]::None), 8, ([System.IO.Ports.StopBits]::One)
    # ReadTimeout stays at its default (infinite): with that, the serial
    # driver completes a read as soon as any byte is available rather than
    # waiting to fill the buffer.
    $serial.Open()
    # Must be set after Open(): most Arduino boards hold the chip in reset
    # until DTR is asserted, and SerialPort defaults it to false.
    $serial.DtrEnable = $true
    $serial.RtsEnable = $true
} catch {
    Write-Host "FAILED to open $ComPort : $_"
    exit 1
}

try {
    $listener = New-Object System.Net.Sockets.TcpListener([System.Net.IPAddress]::Loopback, $ListenPort)
    $listener.Start()
} catch {
    Write-Host "FAILED to listen on port $ListenPort : $_"
    exit 1
}
Write-Host "Bridging $ComPort @ $BaudRate baud <-> tcp/$ListenPort (loopback only)"

$serialStream = $serial.BaseStream
$serialBuf = New-Object byte[] 4096
$netBuf = New-Object byte[] 4096
# Only one serial read may be outstanding at a time, so this pending read
# carries over between TCP clients instead of being reissued.
$serialTask = $serialStream.ReadAsync($serialBuf, 0, $serialBuf.Length)

while ($true) {
    $client = $listener.AcceptTcpClient()
    $client.NoDelay = $true
    Write-Host "Client connected."
    $net = $client.GetStream()
    $netTask = $net.ReadAsync($netBuf, 0, $netBuf.Length)

    try {
        while ($true) {
            $tasks = [System.Threading.Tasks.Task[]]@($serialTask, $netTask)
            $ready = [System.Threading.Tasks.Task]::WaitAny($tasks)

            if ($ready -eq 0) {
                # device -> sim
                $n = $serialTask.Result
                if ($n -gt 0) {
                    $net.Write($serialBuf, 0, $n)
                }
                $serialTask = $serialStream.ReadAsync($serialBuf, 0, $serialBuf.Length)
            } else {
                # sim -> device; 0 bytes means the client hung up
                $n = $netTask.Result
                if ($n -le 0) {
                    break
                }
                $serialStream.Write($netBuf, 0, $n)
                $netTask = $net.ReadAsync($netBuf, 0, $netBuf.Length)
            }
        }
    } catch {
        Write-Host "Connection error: $($_.Exception.Message)"
    }

    $client.Close()
    if ($serialTask.IsFaulted) {
        # e.g. the device was unplugged - nothing left to bridge
        Write-Host "Serial port $ComPort failed, exiting."
        exit 1
    }
    Write-Host "Client disconnected, waiting for reconnect..."
}
