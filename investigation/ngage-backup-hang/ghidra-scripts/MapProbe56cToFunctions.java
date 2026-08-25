import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.util.task.ConsoleTaskMonitor;

import java.io.BufferedReader;
import java.io.FileReader;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

// Round 56: wide trace during !ecomserver's fatal -5 Leave cycles shows 90% of
// activity in drtaeabi.dll (round 52's exception-cleanup machinery). Map hits
// to functions using the drtaeabi_fresh_r34 project (round 34/52), and
// decompile the top few - is this the same __cxa_end_catch-shaped code round
// 52 found, or something different/new for this specific leave code (-5)?
public class MapProbe56cToFunctions extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/probe56c_functions.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        BufferedReader reader = new BufferedReader(new FileReader(
            "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/probe56c_pc.txt"));

        Pattern p = Pattern.compile("pc=(0x[0-9a-f]+)");
        FunctionManager fm = currentProgram.getFunctionManager();
        Map<Function, Integer> funcCounts = new LinkedHashMap<>();
        int noFuncCount = 0;

        String line;
        while ((line = reader.readLine()) != null) {
            Matcher m = p.matcher(line);
            if (m.find()) {
                long addr = Long.parseUnsignedLong(m.group(1).substring(2), 16);
                Address a;
                try {
                    a = toAddr(addr);
                } catch (Exception e) {
                    continue;
                }
                if (!currentProgram.getMemory().contains(a)) {
                    continue;
                }
                Function f = fm.getFunctionContaining(a);
                if (f == null) {
                    // Most of this module was never auto-analyzed into function
                    // boundaries (only a couple of specific addresses were
                    // force-created in earlier rounds) - force it now so we get
                    // a real function grouping instead of counting everything
                    // as "no function".
                    try {
                        disassemble(a);
                        createFunction(a, null);
                        f = fm.getFunctionContaining(a);
                    } catch (Exception e) {
                        // ignore, leave as no-function
                    }
                }
                if (f != null) {
                    funcCounts.merge(f, 1, Integer::sum);
                } else {
                    noFuncCount++;
                }
            }
        }
        reader.close();

        out.printf("(addresses outside this module or with no function: %d)%n%n", noFuncCount);
        out.println("=== function hit counts (drtaeabi.dll) ===");
        funcCounts.entrySet().stream()
            .sorted((e1, e2) -> e2.getValue() - e1.getValue())
            .forEach(e -> out.printf("%s @ %s : %d hits%n", e.getKey().getName(), e.getKey().getEntryPoint(), e.getValue()));

        out.println();
        out.println("=== decompiling top 8 functions ===");

        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        ConsoleTaskMonitor monitor2 = new ConsoleTaskMonitor();

        funcCounts.entrySet().stream()
            .sorted((e1, e2) -> e2.getValue() - e1.getValue())
            .limit(8)
            .forEach(e -> {
                Function f = e.getKey();
                out.printf("--- FUNCTION %s @ %s (hits=%d, size=%d) ---%n", f.getName(), f.getEntryPoint(), e.getValue(), f.getBody().getNumAddresses());
                DecompileResults res = decomp.decompileFunction(f, 30, monitor2);
                if (res.decompileCompleted()) {
                    out.println(res.getDecompiledFunction().getC());
                } else {
                    out.println("decompile failed: " + res.getErrorMessage());
                }
                out.println();
            });

        out.close();
        println("WROTE " + outPath);
    }
}
