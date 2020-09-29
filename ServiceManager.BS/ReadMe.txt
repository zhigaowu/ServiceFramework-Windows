sc create "MySampleService1" binPath="E:\Workbench\local\ServiceFramework\ServiceLoader\bin\ServiceLoader.exe --log.level=trace --log.type=daily --log.keep=10 --service.lib=modules\ModuleSample.dll --service.config=conf\ModuleSample.json" start=auto displayname="My Sample Service1" depend=

sc create "MySampleService1"
sc delete "MySampleService1"

sc start "MySampleService1"
sc stop "MySampleService1"

sc create "ServiceManager.BS" binPath="E:\Workbench\local\ServiceFramework\ServiceLoader\bin\ServiceLoader.exe --log.level=trace --log.type=daily --log.keep=10 --service.lib=services\ServiceManager.BS.dll --service.config=data\ServiceManager.BS.json" start=auto displayname="Http service for XXX services management"

sc create "ServiceManager.BS"
sc delete "ServiceManager.BS"

sc start "ServiceManager.BS"
sc stop "ServiceManager.BS"