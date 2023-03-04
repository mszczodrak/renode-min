// 
// Tables.cs
//  
// Author:
//       Lluis Sanchez Gual <lluis@xamarin.com>
// 
// Copyright (c) 2011 Xamarin Inc
// 
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
using System;
using Xwt;

namespace Samples
{
	public class Tables: VBox
	{
		public Tables ()
		{
			Table t = new Table ();
			
			SimpleBox b = new SimpleBox (200, 20);
			t.Add (b, 0, 0);
			
			b = new SimpleBox (5, 20);
			t.Add (b, 1, 0);
			
			b = new SimpleBox (250, 20);
			t.Add (b, 0, 1, colspan:2, hexpand:true, vexpand:true);
			
			b = new SimpleBox (300, 20);
			t.Add (b, 1, 2, colspan:2);
			
			b = new SimpleBox (100, 20);
			t.Add (b, 2, 3);
			
			b = new SimpleBox (450, 20);
			t.Add (b, 0, 4, colspan:3);
			
			PackStart (t);
			
			HBox box = new HBox ();
			PackStart (box);
			t = new Table ();
			t.Add (new Label ("One:"), 0, 0);
			t.Add (new TextEntry (), 1, 0);
			t.Add (new Label ("Two:"), 0, 1);
			t.Add (new TextEntry (), 1, 1);
			t.Add (new Label ("Three:"), 0, 2);
			t.Add (new TextEntry (), 1, 2);
			t.InsertRow (1, 2);
			t.Add (new Label ("One-and-a-half"), 0, 1);
			t.Add (new TextEntry () { PlaceholderText = "Just inserted" }, 1, 1);
			t.InsertRow (1, 2);
			t.Add (new SimpleBox (300, 20), 0, 1, colspan:2);
			box.PackStart (t);
		}
	}
}

