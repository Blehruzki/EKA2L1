import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.ReferenceManager;

import java.io.FileWriter;
import java.io.PrintWriter;

public class FindDataRefs80778990 extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/datarefs_80778990.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        long[] candidates = { 0x80778990L, 0x80778991L };
        ReferenceManager rm = currentProgram.getReferenceManager();
        FunctionManager fm = currentProgram.getFunctionManager();

        for (long c : candidates) {
            Address a = toAddr(c);
            out.printf("=== refs to %s ===%n", a);
            ReferenceIterator refs = rm.getReferencesTo(a);
            int count = 0;
            while (refs.hasNext()) {
                Reference r = refs.next();
                Address from = r.getFromAddress();
                Function f = fm.getFunctionContaining(from);
                out.printf("  ref from %s (in %s) type=%s%n", from,
                    f != null ? f.getName() + "@" + f.getEntryPoint() : "???", r.getReferenceType());
                count++;
            }
            out.printf("  total: %d%n%n", count);
        }

        out.close();
        println("WROTE " + outPath);
    }
}
