import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressIterator;
import ghidra.program.model.symbol.SymbolTable;

import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.HashSet;
import java.util.Set;

public class CheckEuserExports extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/euser_exports_check_r60.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        SymbolTable st = currentProgram.getSymbolTable();

        long[] candidates = {
            0x803aa998L, 0x803a2e18L, 0x803a57ceL, 0x803af5caL, 0x803af60aL,
            0x803a84ccL, 0x803a846aL, 0x803a848eL, 0x803a8be6L, 0x803ac7e2L,
            0x803a82d4L
        };

        out.println("=== all exported symbols (external entry points) ===");
        int total = 0;
        Set<Long> exportedAddrs = new HashSet<>();
        AddressIterator addrIter = st.getExternalEntryPointIterator();
        while (addrIter.hasNext()) {
            Address a = addrIter.next();
            out.printf("  export @ %s%n", a);
            exportedAddrs.add(a.getOffset());
            total++;
        }
        out.printf("total exports: %d%n%n", total);

        out.println("=== checking candidates against export list ===");
        for (long c : candidates) {
            boolean isExport = exportedAddrs.contains(c);
            out.printf("  0x%x exported=%b%n", c, isExport);
        }

        out.close();
        println("WROTE " + outPath);
    }
}
