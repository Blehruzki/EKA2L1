import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.listing.Listing;
import ghidra.util.task.ConsoleTaskMonitor;

import java.io.FileWriter;
import java.io.PrintWriter;

// Round 39 continued: found ecomserver.exe's own import thunks into bafl.dll -
// thunk_EXT_FUN_80778118 (@0x804cd35c) and thunk_EXT_FUN_807781f4 (@0x804cd354),
// both very close to round 25's FUN_8077811a/FUN_80778190. Find whoever in
// ecomserver.exe's own code calls these thunks, and dump that caller in full -
// that's ecomserver's own decision point for whether to talk to bafl.dll/
// !BackupServer at all.
public class FindEcomserverBaflThunkCallers extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/ecomserver_bafl_thunk_callers.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        Listing listing = currentProgram.getListing();
        FunctionManager fm = currentProgram.getFunctionManager();

        Address[] thunks = { toAddr(0x804cbc64L) };

        for (Address thunk : thunks) {
            out.printf("=== callers of thunk @ %s ===%n", thunk);
            InstructionIterator ins = listing.getInstructions(true);
            while (ins.hasNext()) {
                Instruction i = ins.next();
                for (Address ref : i.getFlows()) {
                    if (ref.equals(thunk)) {
                        Function f = fm.getFunctionContaining(i.getAddress());
                        out.printf("%s (in %s): %s%n", i.getAddress(),
                            f != null ? f.getName() + "@" + f.getEntryPoint() + " size=" + f.getBody().getNumAddresses() : "???",
                            i.toString());
                    }
                }
            }
            out.println();
        }

        out.close();
        println("WROTE " + outPath);
    }
}
