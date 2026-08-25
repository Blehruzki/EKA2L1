import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.ReferenceManager;

import java.io.FileWriter;
import java.io.PrintWriter;

public class FindDrmBaflCallers extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/drm_bafl_callers_r69.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        FunctionManager fm = currentProgram.getFunctionManager();
        ReferenceManager rm = currentProgram.getReferenceManager();

        // thunk local addresses (within gametitlemetadata.dll) for drmcommon/drmrights/estor/bafl imports
        long[] thunks = {
            0x70805178L, 0x70805180L, 0x70805190L, 0x70805198L, 0x708051a0L, 0x708051a8L, 0x708051b0L, 0x708051b8L, // estor.dll
            0x708051c8L, 0x708051d0L, // bafl.dll
            0x708051d8L, 0x708051e0L, 0x708051e8L, 0x708051f0L, // drmcommon.dll
            0x708051f8L, 0x70805200L, 0x70805208L, 0x70805210L, 0x70805218L, 0x70805220L // drmrights.dll
        };

        for (long t : thunks) {
            Address a = toAddr(t);
            out.printf("=== callers of thunk @ 0x%x ===%n", t);
            ReferenceIterator refs = rm.getReferencesTo(a);
            int count = 0;
            while (refs.hasNext()) {
                Reference r = refs.next();
                Address from = r.getFromAddress();
                Function f = fm.getFunctionContaining(from);
                out.printf("  from %s (in %s)%n", from, f != null ? f.getName() + "@" + f.getEntryPoint() : "???");
                count++;
            }
            out.printf("  total: %d%n%n", count);
        }

        out.close();
        println("WROTE " + outPath);
    }
}
