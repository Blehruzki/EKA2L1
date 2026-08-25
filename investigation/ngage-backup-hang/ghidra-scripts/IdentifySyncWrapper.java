import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;

import java.io.FileWriter;
import java.io.PrintWriter;

public class IdentifySyncWrapper extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/identify_sync_wrapper.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));
        Address a = toAddr(0x803ac84fL);
        Function f = getFunctionContaining(a);
        out.printf("0x803ac84f -> %s%n", f != null ? f.getName() + " @ " + f.getEntryPoint() : "(none)");
        if (f != null) {
            ReferenceIterator refs = currentProgram.getReferenceManager().getReferencesTo(f.getEntryPoint());
            int count = 0;
            while (refs.hasNext()) {
                Reference r = refs.next();
                Address from = r.getFromAddress();
                Function cf = getFunctionContaining(from);
                out.printf("  caller: 0x%s in %s%n", from, cf != null ? cf.getName() + " @ " + cf.getEntryPoint() : "(none)");
                count++;
            }
            out.printf("  total callers of 0x%s: %d%n", f.getEntryPoint(), count);
        }
        out.close();
        println("WROTE " + outPath);
    }
}
