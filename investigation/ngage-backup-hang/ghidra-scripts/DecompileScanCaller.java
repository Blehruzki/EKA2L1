import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.ReferenceManager;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.util.task.ConsoleTaskMonitor;

import java.io.FileWriter;
import java.io.PrintWriter;

public class DecompileScanCaller extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/scan_caller_r70.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        FunctionManager fm = currentProgram.getFunctionManager();
        ReferenceManager rm = currentProgram.getReferenceManager();
        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);

        Address a = toAddr(0x708038aaL);
        Function f = fm.getFunctionAt(a);
        out.printf("=== function %s @ %s size=%d ===%n", f.getName(), f.getEntryPoint(), f.getBody().getNumAddresses());

        ReferenceIterator refs = rm.getReferencesTo(f.getEntryPoint());
        int count = 0;
        out.printf("callers:%n");
        java.util.Set<Address> callerEntries = new java.util.HashSet<>();
        while (refs.hasNext()) {
            Reference r = refs.next();
            Address from = r.getFromAddress();
            Function cf = fm.getFunctionContaining(from);
            out.printf("  from %s (in %s)%n", from, cf != null ? cf.getName() + "@" + cf.getEntryPoint() : "???");
            if (cf != null) callerEntries.add(cf.getEntryPoint());
            count++;
        }
        out.printf("  total: %d%n%n", count);

        DecompileResults res = decomp.decompileFunction(f, 60, new ConsoleTaskMonitor());
        if (res.decompileCompleted()) {
            out.printf("--- decompiled ---%n%s%n", res.getDecompiledFunction().getC());
        } else {
            out.printf("decompile failed: %s%n", res.getErrorMessage());
        }
        out.println();

        for (Address entry : callerEntries) {
            Function cf = fm.getFunctionAt(entry);
            out.printf("=== caller-of-caller %s @ %s size=%d ===%n", cf.getName(), cf.getEntryPoint(), cf.getBody().getNumAddresses());
            DecompileResults res2 = decomp.decompileFunction(cf, 60, new ConsoleTaskMonitor());
            if (res2.decompileCompleted()) {
                out.printf("--- decompiled ---%n%s%n", res2.getDecompiledFunction().getC());
            } else {
                out.printf("decompile failed: %s%n", res2.getErrorMessage());
            }
            out.println();
        }

        out.close();
        println("WROTE " + outPath);
    }
}
