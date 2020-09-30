# ServiceFramework-Windows
### Simple Application Service Framework For Windows

* Wrap application to Windows Service only by implement the following three interfaces:

1. SERVICE_MODULE_C_API int __cdecl Service_Create(std::shared_ptr<spdlog::logger>& log, const std::string& root, const std::string& config_file_path);
2. SERVICE_MODULE_C_API void __cdecl Service_Run(IsServiceRunning Is_Service_Running);
3. SERVICE_MODULE_C_API void __cdecl Service_Destroy();

* All services can be managed by web

### Web GUI
![service_choose_gui](https://user-images.githubusercontent.com/4556201/94521870-510fb800-0261-11eb-8bfe-5f73013e0118.png)
![service_adding_gui](https://user-images.githubusercontent.com/4556201/94521888-55d46c00-0261-11eb-9280-a0f2bc5b3b13.png)
![service_added_result](https://user-images.githubusercontent.com/4556201/94521900-5a008980-0261-11eb-9d72-aa02b4ebe23a.png)

### Notes:
logic is simple, so there are some limits:
1. the web does not contain any authentication or authorization;
2. no strict parameter validation;
3. no sychronization of multiple user operation

