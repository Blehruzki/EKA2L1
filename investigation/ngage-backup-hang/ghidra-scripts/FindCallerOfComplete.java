import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileOptions;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;

import java.io.FileWriter;
import java.io.PrintWriter;

public class FindCallerOfComplete extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/caller_of_complete.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        DecompInterface decomp = new DecompInterface();
        decomp.setOptions(new DecompileOptions());
        decomp.openProgram(currentProgram);

        Address a = toAddr(0x804cc370L);
        ReferenceIterator refs = currentProgram.getReferenceManager().getReferencesTo(a);
        java.util.Set<Function> seen = new java.util.HashSet<>();
        while (refs.hasNext()) {
            Reference r = refs.next();
            Address from = r.getFromAddress();
            Function f = getFunctionContaining(from);
            out.printf("caller from 0x%s in %s%n", from, f != null ? f.getName() + " @ " + f.getEntryPoint() : "(none)");
            if (f != null && seen.add(f)) {
                DecompileResults res = decomp.decompileFunction(f, 20, monitor);
                if (res != null && res.decompileCompleted()) {
                    out.println(res.getDecompiledFunction().getC());
                }
            }
        }
        decomp.dispose();
        out.close();
        println("WROTE " + outPath);
    }
}
