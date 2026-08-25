import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.symbol.Reference;

import java.io.FileWriter;
import java.io.PrintWriter;

public class FindCcdToMetadataCalls extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/ccd_to_metadata_calls_r70.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        FunctionManager fm = currentProgram.getFunctionManager();
        Listing listing = currentProgram.getListing();

        long lo = 0x70800000L;
        long hi = 0x70809000L;

        InstructionIterator it = listing.getInstructions(true);
        int count = 0;
        while (it.hasNext()) {
            Instruction ins = it.next();
            Reference[] refs = ins.getReferencesFrom();
            for (Reference r : refs) {
                if (!r.getReferenceType().isCall() && !r.getReferenceType().isFlow()) continue;
                long to = r.getToAddress().getOffset();
                if (to >= lo && to < hi) {
                    Function f = fm.getFunctionContaining(ins.getAddress());
                    out.printf("%s (in %s) -> 0x%x  [%s]%n", ins.getAddress(), f != null ? f.getName() + "@" + f.getEntryPoint() : "???", to, ins.toString());
                    count++;
                }
            }
        }
        out.printf("%ntotal calls into gametitlemetadata.dll range: %d%n", count);

        out.close();
        println("WROTE " + outPath);
    }
}
