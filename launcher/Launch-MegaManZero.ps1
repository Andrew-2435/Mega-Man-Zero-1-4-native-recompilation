param(
  [switch]$Console
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$script:games = @(
  @{ Name = 'Mega Man Zero';     Path = (Join-Path $root 'MegaManZeroRecomp\build\MegaManZeroRecomp.exe');  WorkDir = (Join-Path $root 'MegaManZeroRecomp') },
  @{ Name = 'Mega Man Zero 2';   Path = (Join-Path $root 'games\mmz2-recomp\build\MegaManZero2Recomp.exe'); WorkDir = (Join-Path $root 'games\mmz2-recomp') },
  @{ Name = 'Mega Man Zero 3';   Path = (Join-Path $root 'games\mmz3-recomp\build\MegaManZero3Recomp.exe'); WorkDir = (Join-Path $root 'games\mmz3-recomp') },
  @{ Name = 'Mega Man Zero 4';   Path = (Join-Path $root 'games\mmz4-recomp\build\MegaManZero4Recomp.exe'); WorkDir = (Join-Path $root 'games\mmz4-recomp') }
)
$games = $script:games

$script:defaultControls = @(
  @{ Action = 'D-Pad Arriba';    Key = 'Flecha Arriba'; Name = 'up' },
  @{ Action = 'D-Pad Abajo';     Key = 'Flecha Abajo';  Name = 'down' },
  @{ Action = 'D-Pad Izquierda'; Key = 'Flecha Izquierda'; Name = 'left' },
  @{ Action = 'D-Pad Derecha';   Key = 'Flecha Derecha'; Name = 'right' },
  @{ Action = 'Botón A';         Key = 'X';             Name = 'a' },
  @{ Action = 'Botón B';         Key = 'Z';             Name = 'b' },
  @{ Action = 'Botón L';         Key = 'C';             Name = 'l' },
  @{ Action = 'Botón R';         Key = 'V';             Name = 'r' },
  @{ Action = 'Start';           Key = 'Return';        Name = 'start' },
  @{ Action = 'Select';          Key = 'Right Shift';   Name = 'select' }
)

$bindNameToScancode = @{
  'Up'='SDL_SCANCODE_UP'; 'Down'='SDL_SCANCODE_DOWN'; 'Left'='SDL_SCANCODE_LEFT'; 'Right'='SDL_SCANCODE_RIGHT';
  'Return'='SDL_SCANCODE_RETURN'; 'Enter'='SDL_SCANCODE_RETURN'; 'Right Shift'='SDL_SCANCODE_RSHIFT';
  'Left Shift'='SDL_SCANCODE_LSHIFT'; 'Space'='SDL_SCANCODE_SPACE'; 'Tab'='SDL_SCANCODE_TAB';
  'A'='SDL_SCANCODE_A'; 'B'='SDL_SCANCODE_B'; 'C'='SDL_SCANCODE_C'; 'D'='SDL_SCANCODE_D'; 'E'='SDL_SCANCODE_E';
  'F'='SDL_SCANCODE_F'; 'G'='SDL_SCANCODE_G'; 'H'='SDL_SCANCODE_H'; 'I'='SDL_SCANCODE_I'; 'J'='SDL_SCANCODE_J';
  'K'='SDL_SCANCODE_K'; 'L'='SDL_SCANCODE_L'; 'M'='SDL_SCANCODE_M'; 'N'='SDL_SCANCODE_N'; 'O'='SDL_SCANCODE_O';
  'P'='SDL_SCANCODE_P'; 'Q'='SDL_SCANCODE_Q'; 'R'='SDL_SCANCODE_R'; 'S'='SDL_SCANCODE_S'; 'T'='SDL_SCANCODE_T';
  'U'='SDL_SCANCODE_U'; 'V'='SDL_SCANCODE_V'; 'W'='SDL_SCANCODE_W'; 'X'='SDL_SCANCODE_X'; 'Y'='SDL_SCANCODE_Y'; 'Z'='SDL_SCANCODE_Z'
}
$scancodeToDisplay = @{}
foreach ($k in $bindNameToScancode.Keys) { $scancodeToDisplay[$bindNameToScancode[$k]] = $k }

function Get-SelectedGame {
  param($list)
  if ($list.SelectedIndex -ge 0) { return $script:games[$list.SelectedIndex] }
  return $null
}

function Get-KeybindsPath([hashtable]$g) {
  if (-not $g) { return $null }
  return (Join-Path $g.WorkDir 'keybinds.ini')
}

function Read-Keybinds([string]$path) {
  $map = @{}
  foreach ($c in $script:defaultControls) { $map[$c.Name] = $c.Key }
  if (-not $path -or -not (Test-Path -LiteralPath $path)) { return $map }
  $inP1 = $false
  foreach ($line in Get-Content -LiteralPath $path -ErrorAction SilentlyContinue) {
    $t = $line.Trim()
    if ($t -match '^\[(.+)\]$') { $inP1 = ($Matches[1].ToLower() -eq 'player1'); continue }
    if (-not $inP1 -or $t -match '^[#;]' -or $t -notmatch '=') { continue }
    $parts = $t.Split('=',2)
    $k = $parts[0].Trim().ToLower()
    $v = $parts[1].Trim()
    if ($map.ContainsKey($k)) {
      $map[$k] = $v
    }
  }
  return $map
}

function Save-Keybinds([string]$path, [hashtable]$map) {
  $dir = Split-Path -Parent $path
  if ($dir -and -not (Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
  $lines = @(
    '# Controller Keybinds (player1 / GBA)'
    '# Edita y reinicia el juego para aplicar.'
    ''
    '[player1]'
  )
  foreach ($c in $script:defaultControls) {
    $val = $map[$c.Name]
    if (-not $val) { $val = $c.Key }
    $lines += ("{0,-7} = {1}" -f $c.Name, $val)
  }
  Set-Content -LiteralPath $path -Value $lines -Encoding ASCII
}

function Start-Game([hashtable]$g) {
  if (-not (Test-Path -LiteralPath $g.Path)) {
    $msg = "No se encontró el ejecutable:`n$($g.Path)`n`nCompila el proyecto primero."
    if ($script:ConsoleMode) { Write-Host $msg } else { [System.Windows.Forms.MessageBox]::Show($msg, 'Mega Man Zero Launcher', 'OK', 'Warning') | Out-Null }
    return
  }
  try {
    Start-Process -FilePath $g.Path -WorkingDirectory $g.WorkDir | Out-Null
  } catch {
    $msg = "No se pudo iniciar $($g.Name):`n$($_.Exception.Message)"
    if ($script:ConsoleMode) { Write-Host $msg } else { [System.Windows.Forms.MessageBox]::Show($msg, 'Mega Man Zero Launcher', 'OK', 'Error') | Out-Null }
  }
}

$script:ConsoleMode = [bool]$Console
if ($Console) {
  Write-Host '=== Mega Man Zero Launcher ==='
  for ($i = 0; $i -lt $games.Count; $i++) {
    $g = $games[$i]
    $status = if (Test-Path -LiteralPath $g.Path) { 'disponible' } else { 'no compilado' }
    Write-Host ("[{0}] {1} - {2}" -f ($i + 1), $g.Name, $status)
  }
  Write-Host '[Q] Salir'
  $sel = Read-Host 'Selecciona un juego'
  if ($sel -match '^[Qq]$') { exit 0 }
  if ($sel -notmatch '^[1-4]$') { Write-Host 'Opción no válida'; exit 1 }
  Start-Game $games[[int]$sel - 1]
  exit 0
}

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
[System.Windows.Forms.Application]::EnableVisualStyles()

$bg = [System.Drawing.Color]::FromArgb(18, 20, 28)
$panelBg = [System.Drawing.Color]::FromArgb(28, 31, 42)
$accent = [System.Drawing.Color]::FromArgb(0, 200, 255)
$accent2 = [System.Drawing.Color]::FromArgb(255, 80, 80)
$text = [System.Drawing.Color]::FromArgb(235, 238, 245)
$muted = [System.Drawing.Color]::FromArgb(160, 168, 184)

$form = New-Object System.Windows.Forms.Form
$form.Text = 'Mega Man Zero Recomp Launcher'
$form.Size = New-Object System.Drawing.Size(760, 500)
$form.StartPosition = 'CenterScreen'
$form.FormBorderStyle = 'FixedSingle'
$form.MaximizeBox = $false
$form.BackColor = $bg
$form.ForeColor = $text

$header = New-Object System.Windows.Forms.Panel
$header.Location = New-Object System.Drawing.Point(0, 0)
$header.Size = New-Object System.Drawing.Size(760, 64)
$header.BackColor = [System.Drawing.Color]::FromArgb(12, 14, 20)
$form.Controls.Add($header)

$title = New-Object System.Windows.Forms.Label
$title.Text = 'MEGA MAN ZERO'
$title.Font = New-Object System.Drawing.Font('Segoe UI', 20, [System.Drawing.FontStyle]::Bold)
$title.ForeColor = $accent
$title.AutoSize = $true
$title.Location = New-Object System.Drawing.Point(20, 12)
$header.Controls.Add($title)

$subtitle = New-Object System.Windows.Forms.Label
$subtitle.Text = 'Native Recomp Launcher'
$subtitle.Font = New-Object System.Drawing.Font('Segoe UI', 10)
$subtitle.ForeColor = $muted
$subtitle.AutoSize = $true
$subtitle.Location = New-Object System.Drawing.Point(300, 24)
$header.Controls.Add($subtitle)

$tabs = New-Object System.Windows.Forms.TabControl
$tabs.Location = New-Object System.Drawing.Point(12, 74)
$tabs.Size = New-Object System.Drawing.Size(730, 378)
$tabs.Font = New-Object System.Drawing.Font('Segoe UI', 10)
$form.Controls.Add($tabs)

# --- Play tab ---
$tabPlay = New-Object System.Windows.Forms.TabPage
$tabPlay.Text = 'Jugar'
$tabPlay.BackColor = $bg
$tabPlay.ForeColor = $text
$tabs.TabPages.Add($tabPlay)

$list = New-Object System.Windows.Forms.ListBox
$list.Location = New-Object System.Drawing.Point(16, 16)
$list.Size = New-Object System.Drawing.Size(420, 240)
$list.Font = New-Object System.Drawing.Font('Segoe UI', 11)
$list.BackColor = $panelBg
$list.ForeColor = $text
$list.BorderStyle = 'FixedSingle'
foreach ($g in $games) {
  $status = if (Test-Path -LiteralPath $g.Path) { 'Disponible' } else { 'No compilado' }
  [void]$list.Items.Add("$($g.Name)  —  $status")
}
if ($list.Items.Count -gt 0) { $list.SelectedIndex = 0 }
$tabPlay.Controls.Add($list)

$info = New-Object System.Windows.Forms.Label
$info.Location = New-Object System.Drawing.Point(450, 16)
$info.Size = New-Object System.Drawing.Size(250, 160)
$info.Font = New-Object System.Drawing.Font('Segoe UI', 9)
$info.ForeColor = $muted
$info.Text = "Selecciona un juego y pulsa Jugar.`n`nCada título se inicia con su directorio de trabajo correcto (rutas de ROM/BIOS/game.toml).`n`nMMZ1 usa la build de referencia; MMZ2–MMZ4 están en bring-up iterativo."
$tabPlay.Controls.Add($info)

$pathLabel = New-Object System.Windows.Forms.Label
$pathLabel.Location = New-Object System.Drawing.Point(16, 268)
$pathLabel.Size = New-Object System.Drawing.Size(690, 36)
$pathLabel.Font = New-Object System.Drawing.Font('Consolas', 8.5)
$pathLabel.ForeColor = $muted
$tabPlay.Controls.Add($pathLabel)

$play = New-Object System.Windows.Forms.Button
$play.Text = 'JUGAR'
$play.Size = New-Object System.Drawing.Size(140, 38)
$play.Location = New-Object System.Drawing.Point(450, 190)
$play.Font = New-Object System.Drawing.Font('Segoe UI', 11, [System.Drawing.FontStyle]::Bold)
$play.BackColor = $accent
$play.ForeColor = [System.Drawing.Color]::Black
$play.FlatStyle = 'Flat'
$play.FlatAppearance.BorderSize = 0
$play.Add_Click({ if ($list.SelectedIndex -ge 0) { Start-Game $games[$list.SelectedIndex] } })
$tabPlay.Controls.Add($play)

$openDir = New-Object System.Windows.Forms.Button
$openDir.Text = 'Abrir carpeta'
$openDir.Size = New-Object System.Drawing.Size(120, 30)
$openDir.Location = New-Object System.Drawing.Point(450, 236)
$openDir.Font = New-Object System.Drawing.Font('Segoe UI', 9)
$openDir.BackColor = $panelBg
$openDir.ForeColor = $text
$openDir.FlatStyle = 'Flat'
$openDir.Add_Click({
  if ($list.SelectedIndex -ge 0) {
    $wd = $games[$list.SelectedIndex].WorkDir
    if (Test-Path -LiteralPath $wd) { Start-Process explorer.exe -ArgumentList "`"$wd`"" | Out-Null }
  }
})
$tabPlay.Controls.Add($openDir)

$updatePath = {
  if ($list.SelectedIndex -ge 0) {
    $g = $games[$list.SelectedIndex]
    $exists = if (Test-Path -LiteralPath $g.Path) { 'OK' } else { 'FALTA' }
    $pathLabel.Text = "[$exists] $($g.Path)"
  }
}
$list.Add_SelectedIndexChanged($updatePath)
& $updatePath
$list.Add_DoubleClick({ if ($list.SelectedIndex -ge 0) { Start-Game $games[$list.SelectedIndex] } })

# --- Controls tab ---
$tabControls = New-Object System.Windows.Forms.TabPage
$tabControls.Text = 'Controles'
$tabControls.BackColor = $bg
$tabControls.ForeColor = $text
$tabs.TabPages.Add($tabControls)

$controlsNote = New-Object System.Windows.Forms.Label
$controlsNote.Location = New-Object System.Drawing.Point(16, 10)
$controlsNote.Size = New-Object System.Drawing.Size(690, 36)
$controlsNote.Font = New-Object System.Drawing.Font('Segoe UI', 9)
$controlsNote.ForeColor = $muted
$controlsNote.Text = 'Controles por juego (teclado). Los valores por defecto del runtime gbarecomp son: A=X, B=Z, L=C, R=V, Start=Return, Select=Right Shift, D-Pad=Flechas. Se guardan en keybinds.ini ([player1]) en la carpeta del juego seleccionado.'
$tabControls.Controls.Add($controlsNote)

$gameLabel = New-Object System.Windows.Forms.Label
$gameLabel.Location = New-Object System.Drawing.Point(16, 52)
$gameLabel.Size = New-Object System.Drawing.Size(50, 24)
$gameLabel.Font = New-Object System.Drawing.Font('Segoe UI', 9)
$gameLabel.Text = 'Juego:'
$tabControls.Controls.Add($gameLabel)

$controlsGame = New-Object System.Windows.Forms.ComboBox
$controlsGame.Location = New-Object System.Drawing.Point(70, 48)
$controlsGame.Size = New-Object System.Drawing.Size(360, 28)
$controlsGame.DropDownStyle = 'DropDownList'
$controlsGame.Font = New-Object System.Drawing.Font('Segoe UI', 9)
foreach ($g in $games) { [void]$controlsGame.Items.Add($g.Name) }
if ($controlsGame.Items.Count -gt 0) { $controlsGame.SelectedIndex = [Math]::Max(0, $list.SelectedIndex) }
$tabControls.Controls.Add($controlsGame)

$grid = New-Object System.Windows.Forms.DataGridView
$grid.Location = New-Object System.Drawing.Point(16, 86)
$grid.Size = New-Object System.Drawing.Size(690, 210)
$grid.BackgroundColor = $panelBg
$grid.ForeColor = $text
$grid.DefaultCellStyle.BackColor = $panelBg
$grid.DefaultCellStyle.ForeColor = $text
$grid.DefaultCellStyle.SelectionBackColor = [System.Drawing.Color]::FromArgb(50, 60, 80)
$grid.DefaultCellStyle.SelectionForeColor = $text
$grid.ColumnHeadersDefaultCellStyle.BackColor = [System.Drawing.Color]::FromArgb(35, 38, 50)
$grid.ColumnHeadersDefaultCellStyle.ForeColor = $accent
$grid.ColumnHeadersDefaultCellStyle.Font = New-Object System.Drawing.Font('Segoe UI', 9, [System.Drawing.FontStyle]::Bold)
$grid.EnableHeadersVisualStyles = $false
$grid.RowHeadersVisible = $false
$grid.AllowUserToAddRows = $false
$grid.AllowUserToDeleteRows = $false
$grid.ReadOnly = $false
$grid.AutoSizeColumnsMode = 'Fill'
$grid.BorderStyle = 'FixedSingle'
$grid.GridColor = [System.Drawing.Color]::FromArgb(50, 55, 70)
[void]$grid.Columns.Add('Action', 'Acción')
[void]$grid.Columns.Add('Key', 'Tecla (nombre SDL, ej. X, Z, Return, Right Shift, Up)')
$grid.Columns[0].ReadOnly = $true

$loadControls = {
  $g = if ($controlsGame.SelectedIndex -ge 0) { $games[$controlsGame.SelectedIndex] } else { $null }
  $grid.Rows.Clear()
  $map = Read-Keybinds (Get-KeybindsPath $g)
  foreach ($c in $defaultControls) {
    $val = $map[$c.Name]; if (-not $val) { $val = $c.Key }
    [void]$grid.Rows.Add($c.Action, $val)
  }
  $controlsPathLabel.Text = if ($g) { 'Archivo: ' + (Get-KeybindsPath $g) } else { '' }
}

$controlsPathLabel = New-Object System.Windows.Forms.Label
$controlsPathLabel.Location = New-Object System.Drawing.Point(16, 302)
$controlsPathLabel.Size = New-Object System.Drawing.Size(690, 18)
$controlsPathLabel.Font = New-Object System.Drawing.Font('Consolas', 8.5)
$controlsPathLabel.ForeColor = $muted
$tabControls.Controls.Add($controlsPathLabel)

$saveControlsBtn = New-Object System.Windows.Forms.Button
$saveControlsBtn.Text = 'Guardar controles'
$saveControlsBtn.Size = New-Object System.Drawing.Size(140, 30)
$saveControlsBtn.Location = New-Object System.Drawing.Point(450, 46)
$saveControlsBtn.Font = New-Object System.Drawing.Font('Segoe UI', 9)
$saveControlsBtn.BackColor = $accent
$saveControlsBtn.ForeColor = [System.Drawing.Color]::Black
$saveControlsBtn.FlatStyle = 'Flat'
$saveControlsBtn.Add_Click({
  $g = if ($controlsGame.SelectedIndex -ge 0) { $games[$controlsGame.SelectedIndex] } else { $null }
  if (-not $g) { return }
  $map = @{}
  for ($i = 0; $i -lt $defaultControls.Count -and $i -lt $grid.Rows.Count; $i++) {
    $name = $defaultControls[$i].Name
    $val = [string]$grid.Rows[$i].Cells[1].Value
    if ($null -eq $val -or $val.Trim() -eq '') { $val = $defaultControls[$i].Key }
    $map[$name] = $val.Trim()
  }
  try {
    Save-Keybinds (Get-KeybindsPath $g) $map
    [System.Windows.Forms.MessageBox]::Show("Controles guardados en:`n$(Get-KeybindsPath $g)`n`nReinicia el juego para aplicar los cambios.", 'Controles', 'OK', 'Information') | Out-Null
  } catch {
    [System.Windows.Forms.MessageBox]::Show($_.Exception.Message, 'Error al guardar', 'OK', 'Error') | Out-Null
  }
})
$tabControls.Controls.Add($saveControlsBtn)

$resetControlsBtn = New-Object System.Windows.Forms.Button
$resetControlsBtn.Text = 'Restablecer'
$resetControlsBtn.Size = New-Object System.Drawing.Size(100, 30)
$resetControlsBtn.Location = New-Object System.Drawing.Point(600, 46)
$resetControlsBtn.Font = New-Object System.Drawing.Font('Segoe UI', 9)
$resetControlsBtn.BackColor = $panelBg
$resetControlsBtn.ForeColor = $text
$resetControlsBtn.FlatStyle = 'Flat'
$resetControlsBtn.Add_Click({ & $loadControls })
$tabControls.Controls.Add($resetControlsBtn)

$controlsGame.Add_SelectedIndexChanged($loadControls)
$list.Add_SelectedIndexChanged({ if ($controlsGame.Items.Count -gt 0 -and $list.SelectedIndex -ge 0) { $controlsGame.SelectedIndex = $list.SelectedIndex } })
& $loadControls

$controlsHint = New-Object System.Windows.Forms.Label
$controlsHint.Location = New-Object System.Drawing.Point(16, 322)
$controlsHint.Size = New-Object System.Drawing.Size(690, 34)
$controlsHint.Font = New-Object System.Drawing.Font('Segoe UI', 8.5)
$controlsHint.ForeColor = $muted
$controlsHint.Text = 'Mando: el runtime puede detectar gamepads SDL2 automáticamente; la configuración/selección avanzada depende del build y sus archivos de configuración (input.ini/config.ini) en la carpeta del juego. Esta sección edita el mapeo de teclado (keybinds.ini).'
$tabControls.Controls.Add($controlsHint)

# --- About tab ---
$tabAbout = New-Object System.Windows.Forms.TabPage
$tabAbout.Text = 'Acerca de'
$tabAbout.BackColor = $bg
$tabAbout.ForeColor = $text
$tabs.TabPages.Add($tabAbout)

$about = New-Object System.Windows.Forms.Label
$about.Location = New-Object System.Drawing.Point(20, 20)
$about.Size = New-Object System.Drawing.Size(680, 280)
$about.Font = New-Object System.Drawing.Font('Segoe UI', 10)
$about.ForeColor = $text
$about.Text = "Mega Man Zero Native Recomp Launcher`n`nSelector unificado para las recomps nativas de Mega Man Zero (MMZ1–MMZ4) basadas en gbarecomp/MegaManZeroRecomp.`n`n• MMZ1: build de referencia (mstan/MegaManZeroRecomp)`n• MMZ2: games\\mmz2-recomp (en bring-up, corpus/discovery iterativo)`n• MMZ3: games\\mmz3-recomp (scaffold)`n• MMZ4: games\\mmz4-recomp (scaffold)`n`nLas ROMs/BIOS deben ser proporcionadas localmente por el usuario y no se redistribuyen.`n`nUsa el ejecutable MegaManZeroLauncher.exe para abrir esta interfaz."
$tabAbout.Controls.Add($about)

$close = New-Object System.Windows.Forms.Button
$close.Text = 'Cerrar'
$close.Size = New-Object System.Drawing.Size(100, 32)
$close.Location = New-Object System.Drawing.Point(630, 310)
$close.Font = New-Object System.Drawing.Font('Segoe UI', 9)
$close.BackColor = $panelBg
$close.ForeColor = $text
$close.FlatStyle = 'Flat'
$close.Add_Click({ $form.Close() })
$tabAbout.Controls.Add($close)

$form.AcceptButton = $play
$form.CancelButton = $close
[void]$form.ShowDialog()
