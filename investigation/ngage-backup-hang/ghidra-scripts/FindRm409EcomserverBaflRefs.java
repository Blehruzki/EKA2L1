import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.listing.Listing;

import java.io.FileWriter;
import java.io.PrintWriter;

// Round 47: does rm-409's ecomserver.exe (a different build, build=0x240 vs
// rm-320's 0x23D, ~4KB smaller) reference bafl.dll's code range AT ALL?
// rm-409's bafl.dll (this device's own build) occupies
// 0x80557a28-0x80561e5c (code_address/code_size via parse_rom.py). Scan every
// instruction in this binary for any flow (call/branch/data ref) landing in
// that range - same technique round 39 used successfully on rm-320's build.
public class FindRm409EcomserverBaflRefs extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/rm409_ecomserver_bafl_refs.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        Address baflStart = toAddr(0x80557a28L);
        Address baflEnd = toAddr(0x80561e5cL);

        Listing listing = currentProgram.getListing();
        FunctionManager fm = currentProgram.getFunctionManager();

        int found = 0;
        InstructionIterator ins = listing.getInstructions(true);
        while (ins.hasNext()) {
            Instruction i = ins.next();
            for (Address ref : i.getFlows()) {
                if (ref.compareTo(baflStart) >= 0 && ref.compareTo(baflEnd) < 0) {
                    Function f = fm.getFunctionContaining(i.getAddress());
                    out.printf("%s (in %s): %s -> %s%n", i.getAddress(),
                        f != null ? f.getName() + "@" + f.getEntryPoint() : "???",
                        i.toString(), ref);
                    found++;
                }
            }
        }

        out.printf("%ntotal flow refs into bafl.dll range: %d%n", found);
        out.close();
        println("WROTE " + outPath + " (found=" + found + ")");
    }
}
