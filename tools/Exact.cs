// exact.cs — 精确复刻原程序的调用
// ManagementObject("root\\WMI", "MICommonInterface.InstanceName='ACPI\\PNP0C14\\MIFS_0'")
using System;
using System.Management;

class Exact
{
    static void Main(string[] args)
    {
        Console.WriteLine("=== 精确复刻原程序调用 ===");
        try
        {
            // 原程序用 ManagementObject(string scope, string path)
            // .NET 的重载解析需要显式构造 ManagementScope / ManagementPath
            ManagementScope scope = new ManagementScope("root\\WMI");
            ManagementPath  path  = new ManagementPath(
                "MICommonInterface.InstanceName='ACPI\\\\PNP0C14\\\\MIFS_0'");
            ManagementObject obj = new ManagementObject(scope, path, null);

            Console.WriteLine("对象构造 OK");
            Console.WriteLine("  Scope = " + obj.Scope.Path.Path);

            byte[] inData = new byte[32];
            inData[2] = 250;   // Get
            inData[3] = 13;    // CPUGPUSYSFanSpeed

            ManagementBaseObject mp = obj.GetMethodParameters("MiInterface");
            Console.WriteLine("GetMethodParameters OK");
            mp["InData"] = inData;
            Console.WriteLine("InData 已设置");

            ManagementBaseObject result = obj.InvokeMethod("MiInterface", mp, null);
            Console.WriteLine("*** InvokeMethod 成功 ***");

            byte[] outData = result["OutData"] as byte[];
            if (outData != null)
            {
                Console.Write("OutData(" + outData.Length + ") = ");
                for (int i = 0; i < outData.Length; i++) Console.Write(outData[i] + " ");
                Console.WriteLine();
            }
            Console.WriteLine("Reserved = " + result["Reserved"]);

            // 读取其它方法
            Console.WriteLine();
            string[] names = { "8=SystemPerMode", "13=CPUGPUSYSFanSpeed", "19=SystemAcType",
                               "20=MaxFanSpeedSwitch", "21=MaxFanSpeed", "22=CPUThermometer",
                               "23=CPUPower" };
            foreach (string e in names)
            {
                byte mid = byte.Parse(e.Split('=')[0]);
                string nm = e.Split('=')[1];
                byte[] ind = new byte[32];
                ind[2] = 250; ind[3] = mid;
                try
                {
                    ManagementBaseObject mp2 = obj.GetMethodParameters("MiInterface");
                    mp2["InData"] = ind;
                    ManagementBaseObject r2 = obj.InvokeMethod("MiInterface", mp2, null);
                    byte[] od = r2["OutData"] as byte[];
                    string s = "";
                    if (od != null) for (int i = 0; i < od.Length && i < 12; i++) s += od[i] + ",";
                    Console.WriteLine(string.Format("{0,-22} OutData=[{1}]", nm, s));
                }
                catch (Exception ex)
                {
                    Console.WriteLine(string.Format("{0,-22} ERR: {1}", nm, ex.Message));
                }
            }
        }
        catch (Exception ex)
        {
            Console.WriteLine("异常: " + ex.Message);
            if (ex.InnerException != null) Console.WriteLine("  内层: " + ex.InnerException.Message);
        }
    }
}
