import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;

import java.io.BufferedReader;
import java.io.FileReader;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

public class DebugAddrParse extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/debug_addr_parse.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        BufferedReader reader = new BufferedReader(new FileReader(
            "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/probe56c_pc.txt"));

        Pattern p = Pattern.compile("pc=(0x[0-9a-f]+)");
        String line;
        int count = 0;
        while ((line = reader.readLine()) != null && count < 10) {
            Matcher m = p.matcher(line);
            if (m.find()) {
                String hexStr = m.group(1);
                long addr = Long.parseUnsignedLong(hexStr.substring(2), 16);
                Address a = toAddr(addr);
                boolean contains = currentProgram.getMemory().contains(a);
                out.printf("raw='%s' parsed_hex=0x%x addr=%s contains=%b%n", hexStr, addr, a, contains);
                count++;
            }
        }
        reader.close();

        Address minA = currentProgram.getMemory().getMinAddress();
        Address maxA = currentProgram.getMemory().getMaxAddress();
        out.printf("program min=%s max=%s%n", minA, maxA);

        out.close();
        println("WROTE " + outPath);
    }
}
