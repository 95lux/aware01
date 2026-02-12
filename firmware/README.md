# Debug Data Logging 

To debug audio tape player buffers, The configuration option `CONFIG_DEBUG_LOGS` can be enabled in `project_config.h`.

This will print each sample of the recording buffer as binary data via ITM_SendChar() via the SWO line.

To safe the transmitted audio buffer as a file, use the python script in dev-tools dir.

Dependendies: 

``` bash
pip install pyocd
```


## 

Check if connected debugger supports auto-detection and is ready to connect:

```bash
pyocd list          
  #   Probe/Board   Unique ID                  Target  
-------------------------------------------------------
  0   STLINK-V3     0051003D3234510733353533   n/a     
```

