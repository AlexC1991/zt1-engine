# Long-running version of zt.ps1: reads one command per line on stdin,
# replies with one line ("ok ..." or "err ...") per command.
#   shot <file.png> | click <x> <y> | move <x> <y> | front
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class W {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [StructLayout(LayoutKind.Sequential)] public struct PT { public int X, Y; }
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern IntPtr FindWindow(string c, string t);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref PT p);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, IntPtr e);
}
"@
[W]::SetProcessDPIAware() | Out-Null

function Origin {
  $h = [W]::FindWindow("Zoo", [NullString]::Value)
  if ($h -eq [IntPtr]::Zero) { throw "no window" }
  [W]::ShowWindow($h, 9) | Out-Null
  [W]::SetForegroundWindow($h) | Out-Null
  $o = New-Object W+PT
  [W]::ClientToScreen($h, [ref]$o) | Out-Null
  return $o
}

[Console]::Out.WriteLine("ready"); [Console]::Out.Flush()
while ($true) {
  $line = [Console]::In.ReadLine()
  if ($null -eq $line -or $line -eq "quit") { break }
  $parts = $line.Split(" ", 2)
  try {
    switch ($parts[0]) {
      "front" { $o = Origin; Start-Sleep -Milliseconds 300; $msg = "ok" }
      "shot" {
        $o = Origin; Start-Sleep -Milliseconds 100
        $bmp = New-Object System.Drawing.Bitmap 800, 600
        $g = [System.Drawing.Graphics]::FromImage($bmp)
        $g.CopyFromScreen($o.X, $o.Y, 0, 0, (New-Object System.Drawing.Size 800, 600))
        $g.Dispose(); $bmp.Save($parts[1], [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
        $msg = "ok"
      }
      { $_ -in "click", "move" } {
        $xy = $parts[1].Split(" "); $o = Origin
        [W]::SetCursorPos($o.X + [int]$xy[0], $o.Y + [int]$xy[1]) | Out-Null
        if ($parts[0] -eq "click") {
          Start-Sleep -Milliseconds 60
          [W]::mouse_event(0x0002, 0, 0, 0, [IntPtr]::Zero); Start-Sleep -Milliseconds 50
          [W]::mouse_event(0x0004, 0, 0, 0, [IntPtr]::Zero)
        }
        $msg = "ok"
      }
      default { $msg = "err unknown" }
    }
  } catch { $msg = "err $($_.Exception.Message)" }
  [Console]::Out.WriteLine($msg); [Console]::Out.Flush()
}
