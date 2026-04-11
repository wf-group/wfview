function Component()
{
    if (systemInfo.productType === "windows") {
        installer.installationFinished.connect(this, Component.prototype.installVCRedist);
    }
}

Component.prototype.createOperations = function()
{
    component.createOperations();

    if (systemInfo.productType === "windows") {
        // Remove old installation if maintenance tool exists
        var maintenanceTool = installer.value("TargetDir") + "/wfviewMaintenanceTool.exe";
        if (installer.fileExists(maintenanceTool) && installer.isInstaller()) {
            installer.execute(maintenanceTool, ["purge", "-c"]);
        }

        component.addOperation("CreateShortcut",
            "@TargetDir@/wfview.exe",
            "@StartMenuDir@/wfview.lnk",
            "workingDirectory=@TargetDir@",
            "iconPath=@TargetDir@/wfview.exe");
    }
}

Component.prototype.installVCRedist = function() {
    var registryVC2019x64 = installer.execute("reg", new Array("QUERY",
        "HKEY_LOCAL_MACHINE\\SOFTWARE\\Wow6432Node\\Microsoft\\VisualStudio\\14.0\\VC\\Runtimes\\x64",
        "/v", "Installed"))[0];
    var doInstall = false;
    if (!registryVC2019x64) {
        doInstall = true;
    } else {
        var bld = installer.execute("reg", new Array("QUERY",
            "HKEY_LOCAL_MACHINE\\SOFTWARE\\Wow6432Node\\Microsoft\\VisualStudio\\14.0\\VC\\Runtimes\\x64",
            "/v", "Bld"))[0];
        var elements = bld.split(" ");
        bld = parseInt(elements[elements.length-1]);
        if (bld < 33810) {
            doInstall = true;
        }
    }

    if (doInstall) {
        QMessageBox.information("vcRedist.install", "Install VS Redistributables",
            "The application requires Visual Studio 2019 Redistributables. Please follow the steps to install it now.",
            QMessageBox.OK);
        var dir = installer.value("TargetDir");
        installer.execute(dir + "/vc_redist.x64.exe", "/norestart", "/passive");
    }
}