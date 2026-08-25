import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;

import java.io.FileWriter;
import java.io.PrintWriter;

public class FindCallerCbc64 extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/caller_cbc64.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));
        Address a = toAddr(0x804cbc64L);
        ReferenceIterator refs = currentProgram.getReferenceManager().getReferencesTo(a);
        int count = 0;
        while (refs.hasNext()) {
            Reference r = refs.next();
            Address from = r.getFromAddress();
            Function f = getFunctionContaining(from);
            out.printf("from 0x%s in %s (type=%s)%n", from, f != null ? f.getName() + " @ " + f.getEntryPoint() : "(none)", r.getReferenceType());
            count++;
        }
        out.printf("total: %d%n", count);
        out.close();
        println("WROTE " + outPath);
    }
}
