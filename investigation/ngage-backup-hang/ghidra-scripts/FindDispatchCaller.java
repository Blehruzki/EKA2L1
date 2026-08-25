import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.ReferenceManager;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.util.task.ConsoleTaskMonitor;

import java.io.FileWriter;
import java.io.PrintWriter;

public class FindDispatchCaller extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/dispatch_caller_r70.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        FunctionManager fm = currentProgram.getFunctionManager();
        ReferenceManager rm = currentProgram.getReferenceManager();
        Listing listing = currentProgram.getListing();
        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);

        Address target = toAddr(0x70803998L);
        out.printf("=== callers of FUN_70803998 ===%n");
        ReferenceIterator refs = rm.getReferencesTo(target);
        java.util.Set<Address> seen = new java.util.HashSet<>();
        int count = 0;
        while (refs.hasNext()) {
            Reference r = refs.next();
            Address from = r.getFromAddress();
            Function f = fm.getFunctionContaining(from);
            out.printf("  from %s (in %s)%n", from, f != null ? f.getName() + "@" + f.getEntryPoint() : "???");
            if (f != null) seen.add(f.getEntryPoint());
            count++;
        }
        out.printf("total: %d%n%n", count);

        for (Address entry : seen) {
            Function f = fm.getFunctionAt(entry);
            out.printf("=== caller %s @ %s size=%d ===%n", f.getName(), f.getEntryPoint(), f.getBody().getNumAddresses());
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
