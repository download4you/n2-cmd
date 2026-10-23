-- Built-in quick commands. A missing unix field disables that command on Unix.
return {
    {
        key = "flush-dns", label = "Flush DNS cache", category = "Network",
        description = "Clear the Windows DNS resolver cache. Requires Administrator.",
        powershell = [[Clear-DnsClientCache]], cmd = [[ipconfig /flushdns]],
        unix = [[sudo resolvectl flush-caches]], admin = true,
    },
    {
        key = "local-ip", label = "Show local IP", category = "Network",
        description = "Show active IPv4 addresses and their adapters.",
        powershell = [[Get-NetIPAddress -AddressFamily IPv4 | Where-Object { $_.IPAddress -ne '127.0.0.1' } | Select-Object InterfaceAlias,IPAddress | Format-Table -AutoSize]],
        cmd = [[ipconfig | findstr /i "IPv4"]], unix = [[ip -4 addr show scope global]],
    },
    {
        key = "public-ip", label = "Show public IP", category = "Network",
        description = "Ask ipify.org for your public IPv4 address.",
        powershell = [[Invoke-RestMethod https://api.ipify.org]],
        cmd = [[powershell -NoProfile -Command "(Invoke-RestMethod https://api.ipify.org)"]],
        unix = [[curl -s https://api.ipify.org]],
    },
    {
        key = "dns-servers", label = "Show DNS servers", category = "Network",
        description = "List the DNS servers configured on each adapter.",
        powershell = [[Get-DnsClientServerAddress | Where-Object { $_.ServerAddresses } | Select-Object InterfaceAlias,ServerAddresses | Format-Table -AutoSize]],
        cmd = [[ipconfig /all | findstr /i "DNS Servers"]], unix = [[cat /etc/resolv.conf]],
    },
    {
        key = "ping", label = "Ping Google DNS", category = "Network",
        description = "Send four pings to 8.8.8.8.",
        powershell = [[Test-Connection 8.8.8.8 -Count 4]], cmd = [[ping 8.8.8.8]],
        unix = [[ping -c 4 8.8.8.8]],
    },
    {
        key = "trace", label = "Trace route to Google", category = "Network",
        description = "Trace the network path to 8.8.8.8.",
        powershell = [[tracert 8.8.8.8]], cmd = [[tracert 8.8.8.8]], unix = [[traceroute 8.8.8.8]],
    },
    {
        key = "lookup", label = "DNS lookup google.com", category = "Network",
        description = "Resolve google.com through DNS.",
        powershell = [[Resolve-DnsName google.com]], cmd = [[nslookup google.com]], unix = [[nslookup google.com]],
    },
    {
        key = "connections", label = "Established connections", category = "Network",
        description = "List active TCP connections and owning process IDs.",
        powershell = [[Get-NetTCPConnection -State Established | Select-Object LocalAddress,LocalPort,RemoteAddress,RemotePort,OwningProcess | Format-Table -AutoSize]],
        cmd = [[netstat -ano | findstr ESTABLISHED]], unix = [[ss -tun state established]],
    },
    {
        key = "listening", label = "Listening ports", category = "Network",
        description = "List TCP ports waiting for incoming connections.",
        powershell = [[Get-NetTCPConnection -State Listen | Select-Object LocalAddress,LocalPort,OwningProcess | Format-Table -AutoSize]],
        cmd = [[netstat -ano | findstr LISTENING]], unix = [[ss -tuln]],
    },
    {
        key = "ipconfig-all", label = "Full network config", category = "Network",
        description = "Show the complete ipconfig report.",
        powershell = [[ipconfig /all]], cmd = [[ipconfig /all]], unix = [[ip addr]],
    },
    {
        key = "wifi-profiles", label = "Saved Wi-Fi networks", category = "Network",
        description = "List Wi-Fi profiles stored on this PC.",
        powershell = [[netsh wlan show profiles]], cmd = [[netsh wlan show profiles]],
    },
    {
        key = "wifi-info", label = "Current Wi-Fi", category = "Network",
        description = "Show the connected Wi-Fi network and signal quality.",
        powershell = [[netsh wlan show interfaces]], cmd = [[netsh wlan show interfaces]],
    },
    {
        key = "sysinfo", label = "System information", category = "System",
        description = "Show computer name, Windows version and installed memory.",
        powershell = [[Get-ComputerInfo | Select-Object CsName,WindowsProductName,OsVersion,CsTotalPhysicalMemory | Format-List]],
        cmd = [[systeminfo]], unix = [[uname -a]],
    },
    {
        key = "uptime", label = "System uptime", category = "System",
        description = "Show how long Windows has been running.",
        powershell = [[(Get-Date) - (Get-CimInstance Win32_OperatingSystem).LastBootUpTime]],
        cmd = [[net statistics workstation | findstr /i "Statistics since"]], unix = [[uptime]],
    },
    {
        key = "disk", label = "Disk free space", category = "System",
        description = "Show used and free space for local drives.",
        powershell = [[Get-PSDrive -PSProvider FileSystem | Select-Object Name,@{n='UsedGB';e={[math]::Round($_.Used/1GB,1)}},@{n='FreeGB';e={[math]::Round($_.Free/1GB,1)}} | Format-Table -AutoSize]],
        cmd = [[wmic logicaldisk get caption,size,freespace]], unix = [[df -h]],
    },
    {
        key = "processes", label = "Top processes by CPU", category = "System",
        description = "Show the busiest processes and their memory use.",
        powershell = [[Get-Process | Sort-Object CPU -Descending | Select-Object -First 12 Name,CPU,WS | Format-Table -AutoSize]],
        cmd = [[tasklist]], unix = [[ps aux --sort=-%cpu | head -15]],
    },
    {
        key = "startup", label = "Startup programs", category = "System",
        description = "List programs that start with Windows.",
        powershell = [[Get-CimInstance Win32_StartupCommand | Select-Object Name,Command,Location | Format-Table -AutoSize]],
        cmd = [[wmic startup get caption,command]],
    },
    {
        key = "whoami", label = "Who am I", category = "System",
        description = "Show the current user and computer name.",
        powershell = [[whoami; hostname]], cmd = [[whoami & hostname]], unix = [[whoami; hostname]],
    },
    {
        key = "cwd", label = "Show current folder", category = "Files",
        description = "Print the current working directory.",
        powershell = [[Get-Location]], cmd = [[cd]], unix = [[pwd]],
    },
    {
        key = "open-folder", label = "Open current folder", category = "Files",
        description = "Open the current directory in File Explorer.",
        powershell = [[explorer .]], cmd = [[explorer .]],
    },
    {
        key = "list-files", label = "List files", category = "Files",
        description = "List the contents of the current folder.",
        powershell = [[Get-ChildItem | Format-Table -AutoSize]], cmd = [[dir]], unix = [[ls -la]],
    },
    {
        key = "largest", label = "Largest items here", category = "Files",
        description = "Show the largest files and folders in the current directory.",
        powershell = [[Get-ChildItem | Sort-Object Length -Descending | Select-Object -First 15 Name,@{n='MB';e={[math]::Round($_.Length/1MB,2)}} | Format-Table -AutoSize]],
        cmd = [[dir /o-s]], unix = [[du -ah . | sort -rh | head -15]],
    },
    {
        key = "downloads", label = "Open Downloads", category = "Files",
        description = "Open your Downloads folder.",
        powershell = [[explorer "$env:USERPROFILE\Downloads"]],
        cmd = [[explorer "%USERPROFILE%\Downloads"]],
    },
    {
        key = "git-status", label = "Git status", category = "Developer",
        description = "Show the working tree status of the current repository.",
        powershell = [[git status]], cmd = [[git status]], unix = [[git status]],
    },
    {
        key = "git-log", label = "Recent Git commits", category = "Developer",
        description = "Show the last 15 commits in one line each.",
        powershell = [[git log --oneline -15]], cmd = [[git log --oneline -15]], unix = [[git log --oneline -15]],
    },
    {
        key = "docker-ps", label = "Docker containers", category = "Developer",
        description = "List running Docker containers.",
        powershell = [[docker ps]], cmd = [[docker ps]], unix = [[docker ps]],
    },
    {
        key = "python-version", label = "Python version", category = "Developer",
        description = "Print the active Python version.",
        powershell = [[python --version]], cmd = [[python --version]], unix = [[python3 --version]],
    },
    {
        key = "node-version", label = "Node version", category = "Developer",
        description = "Print the active Node.js version.",
        powershell = [[node --version]], cmd = [[node --version]], unix = [[node --version]],
    },
}
