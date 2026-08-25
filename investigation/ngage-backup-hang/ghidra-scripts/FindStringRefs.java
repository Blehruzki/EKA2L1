import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.ReferenceManager;

import java.io.FileWriter;
import java.io.PrintWriter;

public class FindStringRefs extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/string_refs_r60.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        FunctionManager fm = currentProgram.getFunctionManager();
        ReferenceManager rm = currentProgram.getReferenceManager();

        long strAddr = 0x700022c4L;
        Address a = toAddr(strAddr);
        out.printf("=== direct refs to string @ 0x%x ===%n", strAddr);
        ReferenceIterator refs = rm.getReferencesTo(a);
        int count = 0;
        while (refs.hasNext()) {
            Reference r = refs.next();
            Address from = r.getFromAddress();
            Function f = fm.getFunctionContaining(from);
            out.printf("  from %s (in %s) type=%s%n", from, f != null ? f.getName() + "@" + f.getEntryPoint() : "???", r.getReferenceType());
            count++;
        }
        out.printf("total direct refs: %d%n%n", count);

        // Also check a bit before/after in case string starts a few bytes earlier/later (alignment)
        for (long off = -8; off <= 8; off += 2) {
            long candidate = strAddr + off;
            if (candidate == strAddr) continue;
            Address ca = toAddr(candidate);
            ReferenceIterator r2 = rm.getReferencesTo(ca);
            int c2 = 0;
            while (r2.hasNext()) {
                Reference r = r2.next();
                Address from = r.getFromAddress();
                Function f = fm.getFunctionContaining(from);
                out.printf("  [offset %d] from %s (in %s) type=%s -> target 0x%x%n", off, from,
                    f != null ? f.getName() + "@" + f.getEntryPoint() : "???", r.getReferenceType(), candidate);
                c2++;
            }
            if (c2 > 0) out.printf("  total at offset %d: %d%n", off, c2);
        }

        out.close();
        println("WROTE " + outPath);
    }
}
