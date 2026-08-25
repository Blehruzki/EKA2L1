import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.mem.Memory;

import java.io.FileWriter;
import java.io.PrintWriter;

public class DumpBytesRaw extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/dump_bytes_r70.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        Memory mem = currentProgram.getMemory();
        Address start = toAddr(0x708047e6L);
        int len = 90;
        byte[] buf = new byte[len];
        mem.getBytes(start, buf);

        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < len; i++) {
            if (i % 16 == 0) {
                if (i != 0) sb.append("\n");
                sb.append(String.format("%08x  ", start.getOffset() + i));
            }
            sb.append(String.format("%02x ", buf[i] & 0xff));
        }
        out.println(sb.toString());
        out.close();
        println("WROTE " + outPath);
    }
}
