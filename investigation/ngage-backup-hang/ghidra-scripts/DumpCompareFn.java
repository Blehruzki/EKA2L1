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

// baksrvs.exe decides "real implementation vs fallback stub" based on the
// return value of this euser.dll function (0x803a7cf2). Dump it (and its
// direct callees, one level deep, since these tend to be thin wrappers) to
// see what it actually compares.
public class DumpCompareFn extends GhidraScript {
    private static final long[] TARGETS = {
        0x803a7cf2L
    };

    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/euser_comparefn_result.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        Listing listing = currentProgram.getListing();
        FunctionManager fm = currentProgram.getFunctionManager();
        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        ConsoleTaskMonitor monitor2 = new ConsoleTaskMonitor();

        java.util.List<Long> toDump = new java.util.ArrayList<>();
        for (long t : TARGETS) toDump.add(t);

        java.util.Set<Long> seen = new java.util.HashSet<>();
        int idx = 0;
        while (idx < toDump.size()) {
            long t = toDump.get(idx);
            idx++;
            if (!seen.add(t)) continue;

            Address addr = toAddr(t);
            Function func = fm.getFunctionContaining(addr);
            out.printf("=== target 0x%x ===%n", t);
            if (func == null) {
                out.println("  no containing function found");
                out.println();
                continue;
            }
            out.printf("FUNCTION %s @ %s size=%d%n", func.getName(), func.getEntryPoint(), func.getBody().getNumAddresses());
            out.println("--- disassembly ---");
            InstructionIterator ins = listing.getInstructions(func.getBody(), true);
            while (ins.hasNext()) {
                Instruction i = ins.next();
                out.printf("%s: %s%n", i.getAddress(), i.toString());
            }
            out.println("--- decompiled ---");
            DecompileResults res = decomp.decompileFunction(func, 60, monitor2);
            if (res.decompileCompleted()) {
                out.println(res.getDecompiledFunction().getC());
                // queue up direct callees one level deep
                for (ghidra.program.model.symbol.Reference ref : currentProgram.getReferenceManager().getReferencesFrom(func.getEntryPoint())) {
                }
            } else {
                out.println("decompile failed: " + res.getErrorMessage());
            }
            out.println();

            // Queue direct call targets within this function body, one level deep only.
            if (t == TARGETS[0]) {
                InstructionIterator ins2 = listing.getInstructions(func.getBody(), true);
                while (ins2.hasNext()) {
                    Instruction i = ins2.next();
                    String mnem = i.getMnemonicString();
                    if (mnem != null && (mnem.startsWith("bl") || mnem.equals("blx"))) {
                        for (Address flow : i.getFlows()) {
                            toDump.add(flow.getOffset());
                        }
                    }
                }
            }
        }

        out.close();
        println("WROTE " + outPath);
    }
}
