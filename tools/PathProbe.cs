// pathprobe.cs — 精确打印并测试路径字符串
using System;
using System.Management;

class PathProbe
{
    static void Test(string label, string path)
    {
        Console.WriteLine("--- " + label + " ---");
        Console.WriteLine("  原始: " + path);
        try
        {
            ManagementObject mo = new ManagementObject(path);
            Console.WriteLine("  绑定OK, InstanceName=" + (mo["InstanceName"] as string));
            try
            {
                ManagementBaseObject mp = mo.GetMethodParameters("MiInterface");
                string pnames = "";
                foreach (PropertyData pd in mp.Properties) pnames += pd.Name + ",";
                Console.WriteLine("  GetMethodParameters OK; 参数: " + pnames);
                byte[] inData = new byte[32];
                inData[2] = 250; inData[3] = 13;
                mp["InData"] = inData;
                ManagementBaseObject r = mo.InvokeMethod("MiInterface", mp, null);
                Console.WriteLine("  *** InvokeMethod 成功 ***");
                byte[] od = r["OutData"] as byte[];
                if (od != null)
                {
                    Console.Write("  OutData(" + od.Length + ") = ");
                    for (int i = 0; i < od.Length && i < 16; i++) Console.Write(od[i] + " ");
                    Console.WriteLine();
                }
                Console.WriteLine("  Reserved=" + r["Reserved"]);
            }
            catch (Exception ex)
            {
                Console.WriteLine("  调用失败: " + ex.Message);
            }
        }
        catch (Exception ex)
        {
            Console.WriteLine("  绑定失败: " + ex.Message);
        }
        Console.WriteLine();
    }

    static void Main()
    {
        string instPlain   = "ACPI\\PNP0C14\\MIFS_0";          // 单反斜杠
        string instEscaped = "ACPI\\\\PNP0C14\\\\MIFS_0";      // 双反斜杠(字面)
        Console.WriteLine("instPlain   = " + instPlain);
        Console.WriteLine("instEscaped = " + instEscaped);
        Console.WriteLine();

        string s = Environment.MachineName;
        Test("A. \\\\machine\\root\\WMI:Class.InstanceName=\"单反斜杠\"",
             "\\\\" + s + "\\root\\WMI:MICommonInterface.InstanceName=\"" + instPlain + "\"");
        Test("B. \\\\machine\\root\\WMI:Class.InstanceName='单反斜杠'",
             "\\\\" + s + "\\root\\WMI:MICommonInterface.InstanceName='" + instPlain + "'");
        Test("C. \\\\machine\\root\\WMI:Class.InstanceName=\"双反斜杠\"",
             "\\\\" + s + "\\root\\WMI:MICommonInterface.InstanceName=\"" + instEscaped + "\"");
        Test("D. \\\\machine\\root\\WMI:Class.InstanceName='双反斜杠'",
             "\\\\" + s + "\\root\\WMI:MICommonInterface.InstanceName='" + instEscaped + "'");
        Test("E. root\\WMI:Class.InstanceName=\"单反斜杠\"",
             "root\\WMI:MICommonInterface.InstanceName=\"" + instPlain + "\"");
        Test("F. root\\WMI:Class.InstanceName='单反斜杠'",
             "root\\WMI:MICommonInterface.InstanceName='" + instPlain + "'");
    }
}
