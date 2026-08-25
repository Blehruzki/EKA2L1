import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.listing.Listing;

import java.io.FileWriter;
import java.io.PrintWriter;

// Round 39: need to know what in ecomserver.exe's own code decides to call into
// bafl.dll (range 0x807780A8-0x8078266C, round 25) at all - that decision point
// is the actual trigger for the whole !BackupServer chain. Scan every instruction
// in the fully-analyzed ecomserver.exe for any reference to bafl.dll's range,
// since a direct call would show up as an import-stub-style "blx <addr>" or
// "bl <addr>" whose target itself thunks into that range, or (more likely, since
// bafl.dll is a separate DLL) an indirect call through an import table entry
// whose resolved value falls in that range.
public class FindEcomserverBaflCallers extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/ecomserver_bafl_callers.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        Listing listing = currentProgram.getListing();
        FunctionManager fm = currentProgram.getFunctionManager();

        Address baflStart = toAddr(0x807780A8L);
        Address baflEnd = toAddr(0x8078266CL);

        out.println("=== instructions with an operand/reference landing in bafl.dll's range ===");
        InstructionIterator ins = listing.getInstructions(true);
        int found = 0;
        while (ins.hasNext() && found < 200) {
            Instruction i = ins.next();
            for (Address ref : i.getFlows()) {
                if (ref.compareTo(baflStart) >= 0 && ref.compareTo(baflEnd) <= 0) {
                    Function f = fm.getFunctionContaining(i.getAddress());
                    out.printf("%s (in %s): %s -> %s%n", i.getAddress(),
                        f != null ? f.getName() + "@" + f.getEntryPoint() : "???",
                        i.toString(), ref);
                    found++;
                }
            }
        }

        out.println();
        out.println("=== also checking raw operand scalars for bafl.dll addresses (covers ldr =addr literal pools) ===");
        ins = listing.getInstructions(true);
        int found2 = 0;
        while (ins.hasNext() && found2 < 200) {
            Instruction i = ins.next();
            int numOps = i.getNumOperands();
            for (int op = 0; op < numOps; op++) {
                Object[] objs = i.getOpObjects(op);
                for (Object o : objs) {
                    if (o instanceof Address) {
                        Address a = (Address) o;
                        if (a.compareTo(baflStart) >= 0 && a.compareTo(baflEnd) <= 0) {
                            Function f = fm.getFunctionContaining(i.getAddress());
                            out.printf("%s (in %s): %s -> operand %s%n", i.getAddress(),
                                f != null ? f.getName() + "@" + f.getEntryPoint() : "???",
                                i.toString(), a);
                            found2++;
                        }
                    }
                }
            }
        }

        out.close();
        println("WROTE " + outPath);
    }
}
