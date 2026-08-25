import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.ReferenceManager;

import java.io.FileWriter;
import java.io.PrintWriter;

public class FindThunkCallers extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/thunk_callers_r60.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        FunctionManager fm = currentProgram.getFunctionManager();
        ReferenceManager rm = currentProgram.getReferenceManager();

        long[] thunks = { 0x70001454L, 0x7000145cL };
        for (long t : thunks) {
            Address a = toAddr(t);
            out.printf("=== callers of thunk @ 0x%x ===%n", t);
            ReferenceIterator refs = rm.getReferencesTo(a);
            int count = 0;
            while (refs.hasNext()) {
                Reference r = refs.next();
                Address from = r.getFromAddress();
                Function f = fm.getFunctionContaining(from);
                out.printf("  from %s (in %s) type=%s%n", from, f != null ? f.getName() + "@" + f.getEntryPoint() : "???", r.getReferenceType());
                count++;
            }
            out.printf("  total: %d%n%n", count);
        }

        out.close();
        println("WROTE " + outPath);
    }
}
