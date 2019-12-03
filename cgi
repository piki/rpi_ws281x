#!/usr/bin/ruby

require 'cgi'

COLORS = %w[red green blue pink purple orange yellow teal gold cool white]

cgi = CGI.new
puts cgi.header

mode = cgi['mode']
rate = cgi['rate']
color = cgi['color']
code = cgi['code']
pattern = cgi['pattern']

LITERAL_MODES = %w[auto parabolic snow christmas candycane rain fire scoot halloween lightning fireflies chase fade rainbow blink]
OTHER_MODES = ["off", "color", "pattern", "color code"]
REGEX = Regexp.new("^" + (LITERAL_MODES+OTHER_MODES).join('|') + "$")

puts %Q(<html>
<head>
<title>RPI WS2811 LED control</title>
</head>
<body>
<meta name="viewport" content="width=device-width, initial-scale=2.2">

<!--
mode=#{mode.inspect}
rate=#{rate.inspect}
color=#{color.inspect}
code=#{code.inspect}
pattern=#{pattern.inspect}
)

# If any input, take action.
if mode =~ REGEX && rate =~ /^\d+$/ && rate.to_i >= 1 && rate.to_i <= 100
	args = nil
	case mode
		when "off"
			args = [ "-r", rate, "-m", "color:000000" ]
		when Regexp.new(LITERAL_MODES.join('|'))
			args = [ "-r", rate, "-m", mode ]
		when "color"
			args = [ "-r", rate, "-m", "color:#{color}" ] if color =~ /^\w+$/
		when "pattern"
			args = [ "-r", rate, "-m", "pattern:#{pattern}" ] if pattern =~ /^(\w+,)*\w+$/
		when "color code"
			args = [ "-r", rate, "-m", "color:#{code}" ] if code =~ /^[0-9a-fA-F]{6}$/
	end
	
	puts "do ittttt: #{args.inspect}"
	if args
		pid = Process.fork
		if pid.nil?
			$stdout.close_on_exec = true
			$stderr.close_on_exec = true
			Process.setsid
			exec("/usr/local/bin/leds", *args)
			exit 1
		else
			Process.detach(pid)
		end
	end
else
	rate = "30"
end

puts "-->"

running_processes = `ps -eo args`.lines.grep(%r{(?<!cgi-bin)/leds})
if running_processes.any?
	puts "Running:<br>\n<pre>\n" + running_processes.join + "</pre>";
else
	puts "Nothing running<br>";
end

puts %Q(
<form method="post">
)
LITERAL_MODES.each do |mode|
	puts %Q(<input type="submit" name="mode" value="#{mode}">)
end
puts %Q(
<br><input type="submit" name="mode" value="color"><select name="color">
)
COLORS.each { |x| puts %Q(<option value="#{x}"#{" selected" if color == x}>#{x}) }
puts %Q(
</select><br>
<input type="submit" name="mode" value="color code"><input name="code" type="text" value="#{code}" size=6><br>
<input type="submit" name="mode" value="pattern"><input name="pattern" type="text" value="#{pattern}" placeholder="red,green,blue" size=20><br>
<input type="submit" name="mode" value="off"><p>
<input type="text" name="rate" value="#{rate}" size=3> fps<br>
</form>
</body>
</html>
)
