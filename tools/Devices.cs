// devices.cs — 枚举 ACPI\PNP0C14 下的所有实例与命名空间变体
using System;
using System.Management;

class Devices
{
    static void Main()
    {
        Console.WriteLine("=== 搜索所有 MICommonInterface 实例 ===");
        try
        {
            ManagementScope scope = new ManagementScope("root\\WMI");
            scope.Connect();
            ObjectQuery q = new ObjectQuery("SELECT * FROM MICommonInterface");
            ManagementObjectSearcher s = new ManagementObjectSearcher(scope, q);
            ManagementObjectCollection col = s.Get();
            Console.WriteLine("实例数: " + col.Count);
            ManagementObject first = null;
            foreach (ManagementObject o in col)
            {
                object inst = null;
                try { inst = o["InstanceName"]; } catch { }
                Console.WriteLine("  InstanceName = " + inst);
                Console.WriteLine("  Path         = " + o.Path.Path);
                if (first == null) first = o;
            }

            if (first == null) { Console.WriteLine("无实例"); return; }

            Console.WriteLine();
            Console.WriteLine("=== 用枚举到的对象直接调用 ===");
            byte[] inData = new byte[32];
            inData[2] = 250; inData[3] = 13;
            try
            {
                ManagementBaseObject mp = first.GetMethodParameters("MiInterface");
                mp["InData"] = inData;
                ManagementBaseObject r = first.InvokeMethod("MiInterface", mp, null);
                Console.WriteLine("*** 成功 ***");
                byte[] od = r["OutData"] as byte[];
                if (od != null)
                {
                    Console.Write("OutData(" + od.Length + ") = ");
                    for (int i = 0; i < od.Length && i < 16; i++) Console.Write(od[i] + " ");
                    Console.WriteLine();
                }
            }
            catch (Exception ex)
            {
                Console.WriteLine("失败: " + ex.Message);
                ManagementException me = ex as ManagementException;
                if (me != null) Console.WriteLine("  ErrorCode = " + me.ErrorCode);
            }

            Console.WriteLine();
            Console.WriteLine("=== 检查是否有多个 ACPI WMI 实例 (AOD / MIFS) ===");
            try
            {
                ManagementObjectSearcher s2 = new ManagementObjectSearcher(
                    "root\\WMI", "SELECT * FROM MICommonInterface");
                foreach (ManagementObject o in s2.Get())
                    Console.WriteLine("  " + o["InstanceName"]);
            }
            catch (Exception ex) { Console.WriteLine("  " + ex.Message); }

        }
        catch (Exception ex)
        {
            Console.WriteLine("异常: " + ex.Message);
        }
    }
}
