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

// Round 56: cycle 24 (the last -12 leave, right before the transition to the
// fatal -5 leaves) is the one cycle with heavy ecomserver.exe-own-code activity.
// Map its 589 unique PC hits to functions (using the existing 589-function
// auto-analysis from round 38), tally hit counts per function, and decompile
// the top few - this is ecomserver.exe's own decision logic right at the
// transition point.
public class MapCycle24ToFunctions extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/cycle24_functions.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        BufferedReader reader = new BufferedReader(new FileReader(
            "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/cycle24_raw.txt"));

        Pattern p = Pattern.compile("pc=(0x[0-9a-f]+)");
        Map<Address, Integer> hitCounts = new LinkedHashMap<>();
        FunctionManager fm = currentProgram.getFunctionManager();
        Map<Function, Integer> funcCounts = new LinkedHashMap<>();

        String line;
        while ((line = reader.readLine()) != null) {
            Matcher m = p.matcher(line);
            if (m.find()) {
                long addr = Long.parseUnsignedLong(m.group(1).substring(2), 16);
                Address a = toAddr(addr);
                Function f = fm.getFunctionContaining(a);
                if (f != null) {
                    funcCounts.merge(f, 1, Integer::sum);
                }
            }
        }
        reader.close();

        out.println("=== function hit counts (ecomserver.exe, cycle 24) ===");
        funcCounts.entrySet().stream()
            .sorted((e1, e2) -> e2.getValue() - e1.getValue())
            .forEach(e -> out.printf("%s @ %s : %d hits%n", e.getKey().getName(), e.getKey().getEntryPoint(), e.getValue()));

        out.println();
        out.println("=== decompiling top 6 functions ===");

        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        ConsoleTaskMonitor monitor2 = new ConsoleTaskMonitor();

        funcCounts.entrySet().stream()
            .sorted((e1, e2) -> e2.getValue() - e1.getValue())
            .limit(6)
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
