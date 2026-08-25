import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.mem.MemoryBlock;

import java.io.FileWriter;
import java.io.PrintWriter;

public class CheckEuserRange extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/euser_range_check.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        for (MemoryBlock b : currentProgram.getMemory().getBlocks()) {
            out.printf("block %s: %s - %s%n", b.getName(), b.getStart(), b.getEnd());
        }

        Address target = toAddr(0x803a9717L);
        out.printf("0x803a9717 contained: %s%n", currentProgram.getMemory().contains(target));

        out.close();
        println("WROTE " + outPath);
    }
}
