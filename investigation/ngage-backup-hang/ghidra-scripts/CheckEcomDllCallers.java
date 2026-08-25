import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;

import java.io.FileWriter;
import java.io.PrintWriter;

public class CheckEcomDllCallers extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/ecomdll_external_callers.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath, true));
        out.println("==== PROGRAM " + currentProgram.getName() + " ====");
        long[] targets = { 0x804c66c4L, 0x804c67feL, 0x804c6706L, 0x804c6630L, 0x804c6688L };
        for (long t : targets) {
            Address a = toAddr(t);
            ReferenceIterator refs = currentProgram.getReferenceManager().getReferencesTo(a);
            int count = 0;
            while (refs.hasNext()) {
                Reference r = refs.next();
                Address from = r.getFromAddress();
                Function f = getFunctionContaining(from);
                out.printf("  0x%x ref from 0x%s in %s%n", t, from, f != null ? f.getName() + " @ " + f.getEntryPoint() : "(none)");
                count++;
            }
            out.printf("  0x%x total: %d%n", t, count);
        }
        out.println();
        out.close();
        println("WROTE " + outPath);
    }
}
