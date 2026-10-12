// ============================================================================
//  MiClient.cs — 用原程序同款 .NET API 调用（GetMethodParameters + InvokeMethod）
//  编译: csc /platform:x64 /out:mi_dotnet.exe MiClient.cs
//  这是验证协议的最可靠路径：与原程序使用完全相同的 API 序列。
// ============================================================================
using System;
using System.Management;
using System.Text;

class MiClient
{
    const string NS   = "root\\WMI";
    const string CLS  = "MICommonInterface";
    const string INST = "ACPI\\PNP0C14\\MIFS_0";
    const string METH = "MiInterface";

    static readonly string[] Names = {
        "8=SystemPerMode", "9=GPUMode", "10=KeyboardType", "11=FnLock",
        "13=CPUGPUSYSFanSpeed", "19=SystemAcType", "20=MaxFanSpeedSwitch",
        "21=MaxFanSpeed", "22=CPUThermometer", "23=CPUPower"
    };

    static int Run(ManagementObject obj, byte method, byte type, byte value, out byte[] outData)
    {
        outData = null;
        byte[] inData = new byte[32];
        inData[2] = type;    // 250=Get / 251=Set
        inData[3] = method;
        inData[4] = value;

        ManagementBaseObject inParams = obj.GetMethodParameters(METH);
        inParams["InData"] = inData;

        ManagementBaseObject result = obj.InvokeMethod(METH, inParams, null);
        if (result == null) return -1;

        object od = result["OutData"];
        if (od is byte[] b) { outData = b; return 0; }
        return -2;
    }

    static void Main(string[] args)
    {
        Console.WriteLine("=== MiClient (.NET WMI) ===");
        try
        {
            // 用与原程序一致的路径字符串构造对象
            // 原程序字符串: "root\\WMI" + "MICommonInterface.InstanceName='ACPI\\PNP0C14\\MIFS_0'"
            string[] pathForms = new string[] {
                "\\\\" + Environment.MachineName + "\\" + NS + ":" + CLS + ".InstanceName='ACPI\\\\PNP0C14\\\\MIFS_0'",
                NS + ":" + CLS + ".InstanceName='ACPI\\\\PNP0C14\\\\MIFS_0'",
                CLS + ".InstanceName='ACPI\\\\PNP0C14\\\\MIFS_0'",
                CLS + ".InstanceName=\"ACPI\\\\PNP0C14\\\\MIFS_0\"",
            };

            ManagementObject target = null;
            foreach (string p in pathForms)
            {
                try
                {
                    ManagementObject mo = new ManagementObject(p);
                    string inst = mo["InstanceName"] as string;
                    Console.WriteLine("路径OK: " + p + "  (InstanceName=" + inst + ")");
                    if (target == null) target = mo;
                }
                catch (Exception ex)
                {
                    Console.WriteLine("路径失败: " + p + " -> " + ex.Message);
                }
            }

            if (target == null)
            {
                // 退回到查询方式
                Console.WriteLine("改为查询方式");
                ManagementObjectSearcher searcher = new ManagementObjectSearcher(
                    new ManagementScope("\\\\.\\" + NS), new ObjectQuery("SELECT * FROM " + CLS));
                foreach (ManagementObject o in searcher.Get())
                {
                    if (target == null) target = o;
                }
            }
            if (target == null) { Console.WriteLine("未找到实例"); return; }

            // 指定命令
            if (args.Length >= 3 && args[0] == "set")
            {
                byte m = byte.Parse(args[1]);
                byte v = byte.Parse(args[2]);
                byte[] od;
                int rc = Run(target, m, 251, v, out od);
                Console.WriteLine($"SET method={m} value={v} rc={rc}");
                if (od != null)
                {
                    Console.Write($"  OutData({od.Length}) = ");
                    for (int i = 0; i < od.Length && i < 12; i++) Console.Write(od[i] + " ");
                    Console.WriteLine();
                }
                // 回读
                rc = Run(target, m, 250, 0, out od);
                if (od != null) Console.WriteLine($"  回读 [4]={od[4]}");
                return;
            }

            // 默认：读取全部
            Console.WriteLine();
            Console.WriteLine($"{"方法",-24}{"rc",-5}{"OutData[0..11]",-4}");
            Console.WriteLine(new string('-', 78));
            foreach (string entry in Names)
            {
                byte mid = byte.Parse(entry.Split('=')[0]);
                string nm = entry.Split('=')[1];
                byte[] od;
                int rc = Run(target, mid, 250, 0, out od);
                if (rc == 0 && od != null)
                {
                    StringBuilder sb = new StringBuilder();
                    for (int i = 0; i < od.Length && i < 12; i++) sb.Append(od[i] + ",");
                    Console.WriteLine($"{nm,-24}{rc,-5}{sb}");
                    // 解析常见复合值
                    if (mid == 13)
                    {
                        // CPU/GPU/SYS 转速通常是 3 个 16 位小端
                        int cpu = od[4] | (od[5] << 8);
                        int gpu = od[6] | (od[7] << 8);
                        int sys = od[8] | (od[9] << 8);
                        Console.WriteLine($"    -> CPU={cpu} GPU={gpu} SYS={sys} RPM");
                    }
                }
                else
                {
                    Console.WriteLine($"{nm,-24}{rc,-5}(失败)");
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
