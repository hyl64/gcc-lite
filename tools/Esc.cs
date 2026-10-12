// esc.cs — 找出与原程序 IL 字符串完全一致的转义
// IL 中的字符串（编译期常量）实际内容为：
//   "root\\WMI"                                        -> root\WMI
//   "MICommonInterface.InstanceName='ACPI\\PNP0C14\\MIFS_0'"
//     -> 字符串里含 双反斜杠 字面（因为 WMI 路径要求转义）
using System;
using System.Management;

class Esc
{
    static void Try(string label, string relPath)
    {
        Console.WriteLine("--- " + label + " ---");
        Console.WriteLine("  relPath 实际内容: " + relPath);
        // 显示每个字符，确认反斜杠数量
        Console.Write("  字符序列: ");
        foreach (char c in relPath) Console.Write(c == '\\' ? "<BS>" : c.ToString());
        Console.WriteLine();
        try
        {
            ManagementObject obj = new ManagementObject(
                new ManagementScope("root\\WMI"),
                new ManagementPath(relPath),
                null);
            Console.WriteLine("  构造 OK");
            byte[] inData = new byte[32];
            inData[2] = 250; inData[3] = 13;
            ManagementBaseObject mp = obj.GetMethodParameters("MiInterface");
            mp["InData"] = inData;
            ManagementBaseObject r = obj.InvokeMethod("MiInterface", mp, null);
            Console.WriteLine("  *** InvokeMethod 成功 ***");
            byte[] od = r["OutData"] as byte[];
            if (od != null)
            {
                Console.Write("  OutData(" + od.Length + ") = ");
                for (int i = 0; i < od.Length && i < 12; i++) Console.Write(od[i] + " ");
                Console.WriteLine();
            }
        }
        catch (Exception ex)
        {
            Console.WriteLine("  失败: " + ex.Message);
        }
        Console.WriteLine();
    }

    static void Main()
    {
        // 变体1：单反斜杠（C# 转义后为 1 个 \）
        Try("V1 单反斜杠", "MICommonInterface.InstanceName='ACPI\\PNP0C14\\MIFS_0'");
        // 变体2：双反斜杠（C# 转义后为 2 个 \\）
        Try("V2 双反斜杠", "MICommonInterface.InstanceName='ACPI\\\\PNP0C14\\\\MIFS_0'");
        // 变体3：双引号 + 单反斜杠
        Try("V3 双引号单反斜杠", "MICommonInterface.InstanceName=\"ACPI\\PNP0C14\\MIFS_0\"");
        // 变体4：双引号 + 双反斜杠
        Try("V4 双引号双反斜杠", "MICommonInterface.InstanceName=\"ACPI\\\\PNP0C14\\\\MIFS_0\"");
    }
}
