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

public class FindGetterCallers extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/getter_callers_r70.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        FunctionManager fm = currentProgram.getFunctionManager();
        ReferenceManager rm = currentProgram.getReferenceManager();
        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);

        long[] getters = { 0x7080409aL, 0x708040baL, 0x708041c8L, 0x70804518L, 0x708040d6L };

        java.util.Set<Address> callerEntries = new java.util.HashSet<>();

        for (long g : getters) {
            Address gAddr = toAddr(g);
            Function gf = fm.getFunctionAt(gAddr);
            out.printf("=== getter %s @ %s ===%n", gf != null ? gf.getName() : "?", gAddr);
            ReferenceIterator refs = rm.getReferencesTo(gAddr);
            int count = 0;
            while (refs.hasNext()) {
                Reference r = refs.next();
                Address from = r.getFromAddress();
                Function cf = fm.getFunctionContaining(from);
                out.printf("  from %s (in %s)%n", from, cf != null ? cf.getName() + "@" + cf.getEntryPoint() : "???");
                if (cf != null) callerEntries.add(cf.getEntryPoint());
                count++;
            }
            out.printf("  total: %d%n%n", count);
        }

        out.printf("=== unique caller functions: %d ===%n%n", callerEntries.size());
        for (Address entry : callerEntries) {
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
