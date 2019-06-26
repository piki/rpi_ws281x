#!/usr/bin/ruby

require 'cgi'

COLORS = %w[red green blue pink purple orange yellow teal gold cool white]

cgi = CGI.new
puts cgi.header

mode = cgi['mode']
rate = cgi['rate']
color = cgi['color']
code = cgi['code']

puts "<!--"
puts "mode=#{mode.inspect}\n"
puts "rate=#{rate.inspect}\n"
puts "color=#{color.inspect}\n"
puts "code=#{code.inspect}\n"

# If any input, take action.
if mode =~ /^(off|chase|fade|rainbow|blink|color|color code)$/ && rate =~ /^\d+$/ && rate.to_i >= 1 && rate.to_i <= 100
	args = nil
	case mode
		when "off"
			args = [ "-r", rate, "-m", "color:000000" ]
		when "chase", "fade", "rainbow", "blink"
			args = [ "-r", rate, "-m", mode ]
		when "color"
			args = [ "-r", rate, "-m", "color:#{color}" ] if color =~ /^\w+$/
		when "color code"
			args = [ "-r", rate, "-m", "color:#{code}" ] if code =~ /^[0-9a-fA-F]{6}$/
	end
	
	puts "do ittttt: #{args.inspect}"
	if args
		system "pkill -f /usr/local/bin/leds"
		pid = Process.fork
		if pid.nil?
			$stdout.close_on_exec = true
			$stderr.close_on_exec = true
			Process.setsid
			exec("/usr/local/bin/leds", "leds", *args)
			exit 1
		else
			Process.detach(pid)
		end
	end
else
	rate = "30"
end

puts %Q(
-->
<form method="post">
<input type="submit" name="mode" value="chase"><br>
<input type="submit" name="mode" value="fade"><br>
<input type="submit" name="mode" value="rainbow"><br>
<input type="submit" name="mode" value="blink"><br>
<input type="submit" name="mode" value="color"><select name="color">
)
COLORS.each { |x| puts %Q(<option value="#{x}"#{" selected" if color == x}>#{x}) }
puts %Q(
</select><br>
<input type="submit" name="mode" value="color code"><input name="code" type="text" value="#{code}" size=6><br>
<input type="submit" name="mode" value="off"><p>
<input type="text" name="rate" value="#{rate}" size=3> fps<br>
</form>)
