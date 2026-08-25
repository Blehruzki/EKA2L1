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

public class DumpMutexOrdFuncs extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/mutex_ord_funcs_r60.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        FunctionManager fm = currentProgram.getFunctionManager();
        Listing listing = currentProgram.getListing();

        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);

        long[] targets = { 0x803a82aaL, 0x803a82c0L, 0x803a82d4L };
        for (long t : targets) {
            Address a = toAddr(t);
            Function f = fm.getFunctionContaining(a);
            out.printf("=== 0x%x ===%n", t);
            if (f == null) {
                out.printf("  no function%n%n");
                continue;
            }
            out.printf("  function %s @ %s size=%d%n", f.getName(), f.getEntryPoint(), f.getBody().getNumAddresses());

            InstructionIterator instrs = listing.getInstructions(f.getBody(), true);
            while (instrs.hasNext()) {
                Instruction i = instrs.next();
                out.printf("    %s: %s%n", i.getAddress(), i);
            }

            DecompileResults res = decomp.decompileFunction(f, 30, new ConsoleTaskMonitor());
            if (res.decompileCompleted()) {
                out.printf("  --- decompiled ---%n%s%n", res.getDecompiledFunction().getC());
            }
            out.println();
        }

        out.close();
        println("WROTE " + outPath);
    }
}
