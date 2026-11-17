using System;
using System.Diagnostics;
using System.IO;
using System.Windows.Forms;

class MegaManZeroLauncher {
    [STAThread]
    static void Main() {
        try {
            string baseDir = AppDomain.CurrentDomain.BaseDirectory;
            string script = Path.Combine(baseDir, "Launch-MegaManZero.ps1");
            if (!File.Exists(script)) {
                script = Path.Combine(baseDir, "launcher", "Launch-MegaManZero.ps1");
            }
            if (!File.Exists(script)) {
                MessageBox.Show("No se encontró Launch-MegaManZero.ps1 junto al launcher.", "Mega Man Zero Launcher", MessageBoxButtons.OK, MessageBoxIcon.Error);
                return;
            }
            var psi = new ProcessStartInfo();
            psi.FileName = "powershell.exe";
            psi.Arguments = "-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File \"" + script + "\"";
            psi.WorkingDirectory = Path.GetDirectoryName(script);
            psi.UseShellExecute = false;
            psi.CreateNoWindow = true;
            Process.Start(psi);
        } catch (Exception ex) {
            MessageBox.Show(ex.ToString(), "Mega Man Zero Launcher", MessageBoxButtons.OK, MessageBoxIcon.Error);
        }
    }
}
