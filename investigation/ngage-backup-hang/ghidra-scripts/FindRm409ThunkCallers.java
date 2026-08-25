import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.listing.Listing;

import java.io.FileWriter;
import java.io.PrintWriter;

// Round 47: for each of rm-409 ecomserver.exe's 24 import thunks into bafl.dll
// (found by FindRm409EcomserverBaflRefs), find whether any static code in the
// binary actually calls that thunk - same test round 39 ran on rm-320's build
// (which found exactly one static caller, a lazy-singleton wrapper, before
// hitting a vtable-dispatch wall one level up).
public class FindRm409ThunkCallers extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/rm409_thunk_callers.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        long[] thunkAddrs = {
            0x8029a898L, 0x8029a8a0L, 0x8029a8a8L, 0x8029a8b0L, 0x8029a8b8L,
            0x8029a8c0L, 0x8029a8c8L, 0x8029a8d0L, 0x8029a8e0L, 0x8029a8e8L,
            0x8029a8f0L, 0x8029a8f8L, 0x8029a908L, 0x8029a910L, 0x8029a918L,
            0x8029a920L, 0x8029a928L, 0x8029a930L, 0x8029a938L, 0x8029a940L,
            0x8029a948L, 0x8029a950L, 0x8029a960L, 0x8029a968L
        };

        Listing listing = currentProgram.getListing();
        FunctionManager fm = currentProgram.getFunctionManager();

        for (long ta : thunkAddrs) {
            Address thunk = toAddr(ta);
            int count = 0;
            InstructionIterator ins = listing.getInstructions(true);
            while (ins.hasNext()) {
                Instruction i = ins.next();
                if (i.getAddress().equals(thunk)) continue; // skip the thunk itself
                for (Address ref : i.getFlows()) {
                    if (ref.equals(thunk)) {
                        Function f = fm.getFunctionContaining(i.getAddress());
                        out.printf("thunk %s <- %s (in %s): %s%n", thunk, i.getAddress(),
                            f != null ? f.getName() + "@" + f.getEntryPoint() : "???", i.toString());
                        count++;
                    }
                }
            }
            if (count == 0) {
                out.printf("thunk %s: NO STATIC CALLERS%n", thunk);
            }
        }

        out.close();
        println("WROTE " + outPath);
    }
}
