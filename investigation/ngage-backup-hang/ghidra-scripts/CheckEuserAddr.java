import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.MemoryBlock;

import java.io.FileWriter;
import java.io.PrintWriter;

// Sanity check: DumpEuserDispatch's output printed instructions starting at
// 0x8039cde0 instead of the requested 0x80399f48 - a ~0x2e98 mismatch. Before
// trusting or distrusting that, verify what's actually loaded at 0x80399f48 in
// this project: which memory block it falls in, what raw bytes are there, and
// whether disassemble()/clearListing() on it actually took effect.
public class CheckEuserAddr extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/check_euser_addr.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        Address target = toAddr(0x80399f48L);
        out.println("toAddr(0x80399f48) = " + target);

        MemoryBlock block = currentProgram.getMemory().getBlock(target);
        out.println("containing block = " + (block == null ? "null" : block.getName() + " [" + block.getStart() + " - " + block.getEnd() + "] executable=" + block.isExecute()));

        Function f = currentProgram.getFunctionManager().getFunctionContaining(target);
        out.println("containing function (before any edits) = " + (f == null ? "null" : f.getName() + " @ " + f.getEntryPoint()));

        try {
            byte[] buf = new byte[0x40];
            int n = currentProgram.getMemory().getBytes(target, buf);
            StringBuilder sb = new StringBuilder();
            for (int i = 0; i < n; i++) {
                if (i % 16 == 0) sb.append(String.format("%n%s: ", target.add(i)));
                sb.append(String.format("%02x ", buf[i] & 0xff));
            }
            out.println("raw bytes at target:" + sb);
        } catch (Exception e) {
            out.println("getBytes failed: " + e);
        }

        out.println();
        out.println("all memory blocks in this program:");
        for (MemoryBlock b : currentProgram.getMemory().getBlocks()) {
            out.printf("  %s: %s - %s executable=%s%n", b.getName(), b.getStart(), b.getEnd(), b.isExecute());
        }

        out.close();
        println("WROTE " + outPath);
    }
}
