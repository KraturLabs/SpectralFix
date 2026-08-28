# Contributing

Bug reports should include the Ashita interface version, D3D8 wrapper if any,
background resolution, reproduction steps, and the relevant
`logs/spectralfix/spectralfix.log`. Remove account names and other private details
before attaching logs.

Code changes should remain narrowly scoped and include focused tests. Before a
pull request, build both supported ABIs:

```powershell
./scripts/Build-All.ps1 `
  -Ashita416SdkPath 'C:\path\to\ashita-4.16\plugins\sdk' `
  -Ashita430SdkPath 'C:\path\to\ashita-4.30\plugins\sdk'
```

The build uses `/W4 /WX`; warnings fail the build. Do not add client files,
wrapper DLLs, SDK copies, configs, logs, or generated packages to commits.
