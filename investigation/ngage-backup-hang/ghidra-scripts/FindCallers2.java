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

// Reuses the already-built combined euser.dll+ecom.dll+ecomserver.exe program
// (already analyzed) - just searches for callers of a new watch list, no
// re-import / re-analysis.
public class FindCallers2 extends GhidraScript {
    private static final long[] WATCH = {
        0x803aa9dcL, 0x803aaa76L, 0x803ac832L
    };

    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/ghidra_combined_result2.txt";
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
            if (!mnem.toLowerCase().startsWith("bl")) continue;

            for (int i = 0; i < ins.getNumOperands(); i++) {
                for (Object o : ins.getOpObjects(i)) {
                    Long target = null;
                    if (o instanceof Address) target = ((Address) o).getOffset();
                    else if (o instanceof Scalar) target = ((Scalar) o).getUnsignedValue();
                    if (target != null && watchSet.contains(target)) {
                        out.printf("CALL at %s -> 0x%x   (%s)%n", ins.getAddress(), target, ins.toString());
                        Function f = fm.getFunctionContaining(ins.getAddress());
                        if (f != null) {
                            callerFuncs.putIfAbsent(f.getEntryPoint().toString(), f);
                        }
                    }
                }
            }
        }
        out.printf("scanned %d instructions total, %d unique caller functions found%n%n", scanned, callerFuncs.size());

        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        ConsoleTaskMonitor monitor2 = new ConsoleTaskMonitor();

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
