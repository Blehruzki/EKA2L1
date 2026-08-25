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

import java.io.FileWriter;
import java.io.PrintWriter;

public class DecompileIteratorCheck extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/iterator_check_r64.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        FunctionManager fm = currentProgram.getFunctionManager();
        Listing listing = currentProgram.getListing();
        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);

        long[] targets = { 0x803a1f20L, 0x803b2a94L, 0x80399f48L };
        for (long t : targets) {
            Address a = toAddr(t);
            Function f = fm.getFunctionContaining(a);
            out.printf("=== 0x%x ===%n", t);
            if (f == null) {
                out.printf("  no function%n%n");
                continue;
            }
            out.printf("function %s @ %s size=%d%n", f.getName(), f.getEntryPoint(), f.getBody().getNumAddresses());
            InstructionIterator instrs = listing.getInstructions(f.getBody(), true);
            int icount = 0;
            while (instrs.hasNext()) {
                Instruction i = instrs.next();
                out.printf("  %s: %s%n", i.getAddress(), i);
                icount++;
                if (icount > 60) { out.printf("  ... truncated%n"); break; }
            }
            DecompileResults res = decomp.decompileFunction(f, 30, new ConsoleTaskMonitor());
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
