//http://stackoverflow.com/questions/4201860/how-to-find-gcf-lcm-on-a-set-of-numbers

//javac LCM.java && java LCM

class LCM
{
	public static void main(String[] args) throws Exception
	{
		if(args.length!=2)
		{
			System.out.println("Least Common Multiple (LCM) of 44100, 8000 is "+lcm(44100,8000));
			System.out.println("\nHint: enter two numbers as arguments.");
		}
		else
		{
			int i1=Integer.parseInt(args[0]);
			int i2=Integer.parseInt(args[1]);

			System.out.println("Least Common Multiple (LCM) "+i1+", "+i2+" is "+lcm(i1,i2));
		}
		System.out.println("\nSee https://en.wikipedia.org/wiki/Least_common_multiple");
	}


	private static long gcd(long a, long b)
	{
		while (b > 0)
		{
			long temp = b;
			b = a % b; // % is remainder
			a = temp;
		}
		return a;
	}

	private static long gcd(long[] input)
	{
		long result = input[0];
		for(int i = 1; i < input.length; i++) result = gcd(result, input[i]);
		return result;
	}

	//Least common multiple is a little trickier, but probably the best approach is reduction by the GCD, which can be similarly iterated:

	private static long lcm(long a, long b)
	{
		return a * (b / gcd(a, b));
	}

	private static long lcm(long[] input)
	{
		long result = input[0];
		for(int i = 1; i < input.length; i++) result = lcm(result, input[i]);
		return result;
	}
}//end class LCM
