import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.scalar.Scalar;
import ghidra.util.task.ConsoleTaskMonitor;

import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.Map;
import java.util.Set;

public class FindCallers extends GhidraScript {
    // known euser.dll wrapper / SVC-stub addresses to search for as call targets
    private static final long[] WATCH = {
        0x803aa99aL, 0x803aa988L, 0x803a02dcL, 0x8039fcccL
    };

    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/ghidra_callers_result.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        Listing listing = currentProgram.getListing();
        FunctionManager fm = currentProgram.getFunctionManager();

        Set<Long> watchSet = new LinkedHashSet<>();
        for (long w : WATCH) watchSet.add(w);

        Map<String, Function> callerFuncs = new LinkedHashMap<>();

        InstructionIterator all = listing.getInstructions(true);
        int scanned = 0;
        while (all.hasNext()) {
            Instruction ins = all.next();
            scanned++;
            String mnem = ins.getMnemonicString();
            if (!(mnem.startsWith("bl") || mnem.startsWith("BL"))) continue;

            for (int i = 0; i < ins.getNumOperands(); i++) {
                Object[] objs = ins.getOpObjects(i);
                for (Object o : objs) {
                    Long target = null;
                    if (o instanceof Address) {
                        target = ((Address) o).getOffset();
                    } else if (o instanceof Scalar) {
                        target = ((Scalar) o).getUnsignedValue();
                    }
                    if (target != null && watchSet.contains(target)) {
                        out.printf("CALL at %s -> 0x%x   (%s)%n", ins.getAddress(), target, ins.toString());
                        Function f = fm.getFunctionContaining(ins.getAddress());
                        if (f != null) {
                            callerFuncs.putIfAbsent(f.getEntryPoint().toString(), f);
                        } else {
                            out.printf("  (no containing function for this call site)%n");
                        }
                    }
                }
            }
        }
        out.printf("scanned %d instructions total%n%n", scanned);

        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        ConsoleTaskMonitor monitor2 = new ConsoleTaskMonitor();

        out.println("########## CALLER FUNCTIONS ##########\n");
        for (Map.Entry<String, Function> e : callerFuncs.entrySet()) {
            Function func = e.getValue();
            out.println("=========================================================");
            out.printf("FUNCTION %s @ %s%n", func.getName(), func.getEntryPoint());
            out.println("=========================================================\n");

            out.println("--- disassembly ---");
            InstructionIterator ins2 = listing.getInstructions(func.getBody(), true);
            while (ins2.hasNext()) {
                Instruction i = ins2.next();
                out.printf("%s: %s%n", i.getAddress(), i.toString());
            }

            out.println("\n--- decompiled pseudocode ---");
            DecompileResults res = decomp.decompileFunction(func, 60, monitor2);
            if (res.decompileCompleted()) {
                out.println(res.getDecompiledFunction().getC());
            } else {
                out.println("decompile failed: " + res.getErrorMessage());
            }
            out.println("\n");
        }

        out.close();
        println("WROTE RESULT TO " + outPath);
    }
}
