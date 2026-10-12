// mi_api.cs — 用官方同款 Microsoft.Management.Infrastructure (MI API) 调用
// 官方程序加载了 Microsoft.Management.Infrastructure.dll + mi.dll/miutils.dll，
// 说明它走的是 MI API，而非旧版 System.Management (WMI COM)。
using System;
using Microsoft.Management.Infrastructure;
using Microsoft.Management.Infrastructure.Options;
using System.Collections.Generic;

class MiApiTest
{
    static void Main(string[] args)
    {
        Console.WriteLine("=== MI API (CIM) 调用测试 ===");
        try
        {
            using (CimSession session = CimSession.Create(null))
            {
                Console.WriteLine("CimSession 创建 OK");

                // 枚举实例
                IEnumerable<CimInstance> insts =
                    session.QueryInstances(@"root\WMI", "WQL",
                                           "SELECT * FROM MICommonInterface");
                CimInstance target = null;
                foreach (CimInstance ci in insts)
                {
                    object nm = null;
                    try { nm = ci.CimInstanceProperties["InstanceName"].Value; } catch {}
                    Console.WriteLine("  实例: " + nm);
                    if (target == null) target = ci;
                }
                if (target == null) { Console.WriteLine("无实例"); return; }

                // 方法调用
                byte[] inData = new byte[32];
                inData[2] = 250;   // Get
                inData[3] = 13;    // CPUGPUSYSFanSpeed

                var inParams = new CimMethodParametersCollection();
                inParams.Add(CimMethodParameter.Create(
                    "InData", inData, CimType.UInt8Array, CimFlags.In));

                Console.WriteLine();
                Console.WriteLine("调用 MiInterface (id=13)...");
                using (CimMethodResult res = session.InvokeMethod(
                           @"root\WMI", target, "MiInterface", inParams))
                {
                    Console.WriteLine("  ReturnValue: " + (res.ReturnValue != null
                        ? res.ReturnValue.Value : "(null)"));

                    foreach (var op in res.OutParameters)
                    {
                        Console.WriteLine("  出参 " + op.Name + " type=" + op.CimType);
                        if (op.Value is byte[] b)
                        {
                            Console.Write("    值(" + b.Length + ") = ");
                            for (int i = 0; i < b.Length && i < 16; i++) Console.Write(b[i] + " ");
                            Console.WriteLine("   [4]=" + (b.Length > 4 ? b[4].ToString() : "-"));
                        }
                        else
                        {
                            Console.WriteLine("    值 = " + op.Value);
                        }
                    }
                }
            }
        }
        catch (Exception ex)
        {
            Console.WriteLine("异常: " + ex.GetType().Name + ": " + ex.Message);
            if (ex.InnerException != null)
                Console.WriteLine("  内层: " + ex.InnerException.Message);
        }
    }
}
