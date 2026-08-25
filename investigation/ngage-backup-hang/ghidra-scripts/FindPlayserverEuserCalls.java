import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.listing.Listing;

import java.io.FileWriter;
import java.io.PrintWriter;

public class FindPlayserverEuserCalls extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/gametitlemetadata_ext_calls_r69.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        Listing listing = currentProgram.getListing();
        FunctionManager fm = currentProgram.getFunctionManager();

        InstructionIterator instrs = listing.getInstructions(true);
        int count = 0;
        int total_instrs = 0;
        while (instrs.hasNext()) {
            Instruction instr = instrs.next();
            total_instrs++;
            Address[] flows = instr.getFlows();
            for (Address target : flows) {
                long t = target.getOffset();
                if (t < 0x70800000L || t >= 0x70809000L) {
                    Function callerFn = fm.getFunctionContaining(instr.getAddress());
                    out.printf("%s: %s -> target 0x%x [caller fn=%s] OUTSIDE range%n",
                        instr.getAddress(), instr, t,
                        callerFn != null ? callerFn.getName() + "@" + callerFn.getEntryPoint() : "???");
                    count++;
                }
            }
        }
        out.printf("%ntotal instructions scanned: %d%n", total_instrs);
        out.printf("total out-of-range flow targets found: %d%n", count);

        out.close();
        println("WROTE " + outPath);
    }
}
