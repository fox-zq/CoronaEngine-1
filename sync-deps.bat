@echo off
setlocal enabledelayedexpansion

:: 获取脚本所在目录
set "SCRIPT_DIR=%~dp0"
cd /d "%SCRIPT_DIR%"

:: 创建 logs 目录（如果不存在）
set "LOG_DIR=%SCRIPT_DIR%logs"
if not exist "%LOG_DIR%" mkdir "%LOG_DIR%"

:: 创建日志文件名（包含日期时间）- 使用 PowerShell 获取时间戳
for /f "usebackq" %%I in (`powershell -NoProfile -Command "Get-Date -Format 'yyyy-MM-dd_HH-mm-ss'"`) do set "TIMESTAMP=%%I"
set "LOG_FILE=%LOG_DIR%\sync-deps_%TIMESTAMP%.log"

:: 初始化日志
echo ============================================== > "%LOG_FILE%"
echo Git Sync Dependencies Log >> "%LOG_FILE%"
echo Start Time: %date% %time% >> "%LOG_FILE%"
echo ============================================== >> "%LOG_FILE%"
echo. >> "%LOG_FILE%"

echo [INFO] Starting git sync for *-src folders...
echo [INFO] Log file: %LOG_FILE%
echo.

:: 统计计数器
set "SUCCESS_COUNT=0"
set "FAIL_COUNT=0"
set "TOTAL_COUNT=0"

:: 遍历当前目录下所有 *-src 文件夹（不递归）
for /d %%D in (*-src) do (
    set /a TOTAL_COUNT+=1
    set "FOLDER_NAME=%%D"
    
    echo ---------------------------------------------- >> "%LOG_FILE%"
    echo [!TOTAL_COUNT!] Processing: %%D >> "%LOG_FILE%"
    echo ---------------------------------------------- >> "%LOG_FILE%"
    
    echo [!TOTAL_COUNT!] Processing: %%D
    
    :: 进入目录
    pushd "%%D"
    
    if exist ".git" (
        :: 获取当前分支名
        for /f "tokens=*" %%B in ('git rev-parse --abbrev-ref HEAD 2^>^&1') do set "BRANCH=%%B"
        
        echo     Current branch: !BRANCH! >> "%LOG_FILE%"
        echo     Current branch: !BRANCH!
        
        :: 如果是 detached HEAD 状态，尝试切换到主分支
        if "!BRANCH!"=="HEAD" (
            echo     [INFO] Detached HEAD detected, trying to checkout default branch... >> "%LOG_FILE%"
            echo     [INFO] Detached HEAD detected, trying to checkout default branch...
            
            :: 先尝试 main 分支
            git checkout main >> "%LOG_FILE%" 2>&1
            if !errorlevel! equ 0 (
                set "BRANCH=main"
                echo     [INFO] Switched to main branch >> "%LOG_FILE%"
                echo     [INFO] Switched to main branch
            ) else (
                :: 再尝试 master 分支
                git checkout master >> "%LOG_FILE%" 2>&1
                if !errorlevel! equ 0 (
                    set "BRANCH=master"
                    echo     [INFO] Switched to master branch >> "%LOG_FILE%"
                    echo     [INFO] Switched to master branch
                ) else (
                    echo     [WARN] Could not switch to main or master, staying in detached HEAD >> "%LOG_FILE%"
                    echo     [WARN] Could not switch to main or master, staying in detached HEAD
                )
            )
        )
        
        :: 执行 git fetch
        echo     Executing: git fetch >> "%LOG_FILE%"
        git fetch >> "%LOG_FILE%" 2>&1
        
        :: 执行 git pull
        echo     Executing: git pull >> "%LOG_FILE%"
        git pull >> "%LOG_FILE%" 2>&1
        
        if !errorlevel! equ 0 (
            echo     [SUCCESS] Sync completed >> "%LOG_FILE%"
            echo     [SUCCESS] Sync completed
            set /a SUCCESS_COUNT+=1
        ) else (
            echo     [FAILED] Sync failed with error code !errorlevel! >> "%LOG_FILE%"
            echo     [FAILED] Sync failed
            set /a FAIL_COUNT+=1
        )
    ) else (
        echo     [SKIPPED] Not a git repository >> "%LOG_FILE%"
        echo     [SKIPPED] Not a git repository
    )
    
    echo. >> "%LOG_FILE%"
    
    :: 返回上级目录
    popd
    echo.
)

:: 输出总结
echo ============================================== >> "%LOG_FILE%"
echo Summary >> "%LOG_FILE%"
echo ============================================== >> "%LOG_FILE%"
echo Total folders: %TOTAL_COUNT% >> "%LOG_FILE%"
echo Success: %SUCCESS_COUNT% >> "%LOG_FILE%"
echo Failed: %FAIL_COUNT% >> "%LOG_FILE%"
echo End Time: %date% %time% >> "%LOG_FILE%"
echo ============================================== >> "%LOG_FILE%"

echo ==============================================
echo Summary
echo ==============================================
echo Total folders: %TOTAL_COUNT%
echo Success: %SUCCESS_COUNT%
echo Failed: %FAIL_COUNT%
echo ==============================================
echo.
echo Log saved to: %LOG_FILE%

endlocal
pause
