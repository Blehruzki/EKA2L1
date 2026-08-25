import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.listing.Listing;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.util.task.ConsoleTaskMonitor;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.ReferenceManager;

import java.io.FileWriter;
import java.io.PrintWriter;

public class DecompileStuckFunc extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/stuck_func_full_r70.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        FunctionManager fm = currentProgram.getFunctionManager();
        Listing listing = currentProgram.getListing();
        ReferenceManager rm = currentProgram.getReferenceManager();
        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);

        // 0x70803848 and 0x70805924 and 0x708041f8 are inside the stuck loop.
        // Find their containing functions (force-create if needed).
        long[] targets = { 0x70803848L, 0x70805924L, 0x708041f8L };
        for (long t : targets) {
            Address a = toAddr(t);
            Function f = fm.getFunctionContaining(a);
            if (f == null) {
                try {
                    if (listing.getInstructionAt(a) == null) {
                        disassemble(a);
                    }
                    f = createFunction(a, null);
                } catch (Exception e) {
                    out.printf("=== 0x%x: force-create failed: %s ===%n%n", t, e.getMessage());
                    continue;
                }
            }
            if (f == null) {
                out.printf("=== 0x%x: still no function ===%n%n", t);
                continue;
            }

            out.printf("=== target 0x%x is in function %s @ %s size=%d ===%n", t, f.getName(), f.getEntryPoint(), f.getBody().getNumAddresses());

            // callers
            ReferenceIterator refs = rm.getReferencesTo(f.getEntryPoint());
            int count = 0;
            out.printf("callers:%n");
            while (refs.hasNext()) {
                Reference r = refs.next();
                Address from = r.getFromAddress();
                Function cf = fm.getFunctionContaining(from);
                out.printf("  from %s (in %s)%n", from, cf != null ? cf.getName() + "@" + cf.getEntryPoint() : "???");
                count++;
            }
            out.printf("  total callers: %d%n%n", count);

            DecompileResults res = decomp.decompileFunction(f, 60, new ConsoleTaskMonitor());
            if (res.decompileCompleted()) {
                out.printf("--- decompiled ---%n%s%n", res.getDecompiledFunction().getC());
            } else {
                out.printf("decompile failed: %s%n", res.getErrorMessage());
            }
            out.println();
        }

        out.close();
        println("WROTE " + outPath);
    }
}
