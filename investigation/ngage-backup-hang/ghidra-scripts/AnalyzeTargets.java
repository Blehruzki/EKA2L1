import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.listing.Listing;
import ghidra.util.task.ConsoleTaskMonitor;

import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.LinkedHashMap;
import java.util.Map;

public class AnalyzeTargets extends GhidraScript {
    private static final long[] TARGETS = {
        0x8039fa9cL, 0x803a02c8L, 0x803a02dcL, 0x8039fcccL, 0x803aa9abL, 0x803aa997L
    };

    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/ghidra_result.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        FunctionManager fm = currentProgram.getFunctionManager();
        Listing listing = currentProgram.getListing();

        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        ConsoleTaskMonitor monitor2 = new ConsoleTaskMonitor();

        Map<String, Function> seenFuncs = new LinkedHashMap<>();

        for (long t : TARGETS) {
            Address addr = toAddr(t);
            Function func = fm.getFunctionContaining(addr);
            out.printf("=== target 0x%x ===%n", t);
            if (func == null) {
                out.println("  no containing function found");
                out.println();
                continue;
            }
            out.printf("  function: %s at %s size=%d%n", func.getName(), func.getEntryPoint(),
                func.getBody().getNumAddresses());
            seenFuncs.putIfAbsent(func.getEntryPoint().toString(), func);
            out.println();
        }

        out.println();
        out.println("########## FULL DUMPS OF EACH UNIQUE CONTAINING FUNCTION ##########");
        out.println();

        for (Map.Entry<String, Function> e : seenFuncs.entrySet()) {
            Function func = e.getValue();
            out.println("=========================================================");
            out.printf("FUNCTION %s @ %s%n", func.getName(), func.getEntryPoint());
            out.println("=========================================================");
            out.println();

            out.println("--- disassembly ---");
            InstructionIterator ins = listing.getInstructions(func.getBody(), true);
            while (ins.hasNext()) {
                Instruction i = ins.next();
                out.printf("%s: %s%n", i.getAddress(), i.toString());
            }

            out.println();
            out.println("--- decompiled pseudocode ---");
            DecompileResults res = decomp.decompileFunction(func, 60, monitor2);
            if (res.decompileCompleted()) {
                out.println(res.getDecompiledFunction().getC());
            } else {
                out.println("decompile failed: " + res.getErrorMessage());
            }
            out.println();
            out.println();
        }

        out.close();
        println("WROTE RESULT TO " + outPath);
    }
}
