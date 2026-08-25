import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.address.AddressSetView;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.ReferenceManager;

import java.io.FileWriter;
import java.io.PrintWriter;

// Round 47: round 25's exact technique, replayed against rm-409's bafl.dll (same
// nominal build/version as rm-320's - 2.1/0x240 - so the internal logic is
// expected to be structurally identical, just at different addresses). Find the
// raw UTF-16LE "!BackupServer" string in this build, then every code reference
// to it, to locate the equivalent of rm-320's FUN_8077811a/FUN_80778190
// (connect-and-retry-10x function).
public class FindRm409BaflStringRefs extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/rm409_bafl_string_refs.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        Memory mem = currentProgram.getMemory();
        byte[] needle = "!BackupServer".getBytes("UTF-16LE");

        Address found = null;
        Address searchStart = mem.getMinAddress();
        while (true) {
            Address hit = mem.findBytes(searchStart, needle, null, true, monitor);
            if (hit == null) break;
            out.printf("string '!BackupServer' at %s%n", hit);
            found = hit;
            searchStart = hit.add(1);
        }

        if (found == null) {
            out.println("STRING NOT FOUND");
            out.close();
            println("WROTE " + outPath);
            return;
        }

        ReferenceManager rm = currentProgram.getReferenceManager();
        FunctionManager fm = currentProgram.getFunctionManager();

        // Find data refs to the string address itself and to nearby words (round 25
        // found the pointers on the stack, one word before/after the exact string
        // start, so scan a small window around it).
        for (long delta = -0x20; delta <= 0x20; delta += 4) {
            Address a = found.add(delta);
            ReferenceIterator refs = rm.getReferencesTo(a);
            while (refs.hasNext()) {
                Reference r = refs.next();
                Address from = r.getFromAddress();
                Function f = fm.getFunctionContaining(from);
                out.printf("ref to %s from %s (in %s)%n", a, from,
                    f != null ? f.getName() + "@" + f.getEntryPoint() : "???");
            }
        }

        out.close();
        println("WROTE " + outPath);
    }
}
