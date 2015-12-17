# steal-time-benchmark

# Build
```
make all
```

# Run
Scenario 1: 2 cgroups with 128 and 1152 cpu.shares respectively and with 4 processes each. Workload is spinning in loop. Expected percentage 90% (1152/(128+1152))
```
./master spin 4:128 4:1152
```

Scenario 2: 2 cgroups with 800 and 200 cpu.shares and with 1 and 4 processes per group respectively. Workload is ebizzy benchmark. Expected percentage 20% (200/(800+200))
```
./master ebizzy 4:800 4:200
```

