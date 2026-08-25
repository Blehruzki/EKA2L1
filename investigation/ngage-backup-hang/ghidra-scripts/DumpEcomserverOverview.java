import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.util.CodeUnitInsertionException;
import ghidra.program.model.data.StringDataType;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;

import java.io.FileWriter;
import java.io.PrintWriter;

// Round 38 continued: entry point is generic toolchain startup glue, not useful.
// With full auto-analysis this time (small ~30KB binary, safe), dump the
// function list and any ASCII strings found - looking for ecomserver's own
// panic/debug strings or named functions that hint at its shutdown logic.
public class DumpEcomserverOverview extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/ecomserver_overview.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        FunctionManager fm = currentProgram.getFunctionManager();
        out.println("=== function count: " + fm.getFunctionCount() + " ===");
        FunctionIterator fit = fm.getFunctions(true);
        int n = 0;
        while (fit.hasNext() && n < 500) {
            Function f = fit.next();
            out.printf("%s: %s size=%d%n", f.getEntryPoint(), f.getName(), f.getBody().getNumAddresses());
            n++;
        }

        out.println();
        out.println("=== ASCII strings (min length 4) ===");
        Memory mem = currentProgram.getMemory();
        for (MemoryBlock block : mem.getBlocks()) {
            byte[] buf = new byte[(int) block.getSize()];
            try {
                block.getBytes(block.getStart(), buf);
            } catch (Exception e) {
                continue;
            }
            StringBuilder cur = new StringBuilder();
            long strStart = -1;
            for (int i = 0; i < buf.length; i++) {
                byte b = buf[i];
                if (b >= 0x20 && b < 0x7f) {
                    if (cur.length() == 0) strStart = block.getStart().getOffset() + i;
                    cur.append((char) b);
                } else {
                    if (cur.length() >= 4) {
                        out.printf("0x%x: %s%n", strStart, cur.toString());
                    }
                    cur.setLength(0);
                }
            }
            if (cur.length() >= 4) {
                out.printf("0x%x: %s%n", strStart, cur.toString());
            }
        }

        out.close();
        println("WROTE " + outPath);
    }
}
