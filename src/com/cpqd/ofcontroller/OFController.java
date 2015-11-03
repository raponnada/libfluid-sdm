package com.cpqd.ofcontroller;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;

import android.os.Bundle;
import android.os.Handler;
import android.app.Activity;
import android.text.method.ScrollingMovementMethod;
import android.view.View;
import android.view.View.OnClickListener;
import android.view.ViewGroup.LayoutParams;
import android.widget.Button;
import android.widget.EditText;
import android.widget.ScrollView;
import android.widget.TextView;

class Controller implements Runnable {
	static {
		System.loadLibrary("gnustl_shared");
		System.loadLibrary("ofcontroller");
	}

	public void run() {
		startController(6653, 4);
	}

	native void startController(int port, int nthreads);
	
}

class Ofswitch implements Runnable {
	public String ip;
	public Ofswitch(String ip){
		this.ip = ip; 
	}
	/*static {
		System.loadLibrary("gnustl_shared");
		System.loadLibrary("ofcontroller");
	}*/
	public void run() {
		startSwitch(this.ip, 6653);
	}

	native void startSwitch(String ip, int port);
}
public class OFController extends Activity {
	TextView tv;
	EditText et;
	Button bt;
	private int logUpdaterInterval = 1000;
	private Handler logUpdaterHandler;
	Runnable logUpdater = new Runnable() {
		@Override
		public void run() {
			updateLog();
			logUpdaterHandler.postDelayed(logUpdater, logUpdaterInterval);
		}
	};
	
	@Override
	public void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);
		
		// Create a thread for controller and start it
		Thread t = new Thread(new Controller());
		t.start();
		
		/*tv = new TextView(this);
		ScrollView sv = new ScrollView(this);
		sv.addView(tv);
		sv.setLayoutParams(new LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.MATCH_PARENT));
		tv.setMovementMethod(new ScrollingMovementMethod());*/
		
		setContentView(R.layout.activity_ofcontroller);
		tv = (TextView) findViewById(R.id.textView1);
		et = (EditText) findViewById(R.id.editText1);
		bt = (Button) findViewById(R.id.button1);
		
		bt.setOnClickListener(new OnClickListener() {
				@Override
				public void onClick(View v) {
					// Connect to switch
					// Create a thread for switch and start it
					Thread ts = new Thread(new Ofswitch(et.getText().toString()));
					ts.start();	
				}
	    });
		
		//tv.setMovementMethod(new ScrollingMovementMethod());
		logUpdaterHandler = new Handler();
		logUpdater.run();
	}

	void updateLog() {
		String pid = android.os.Process.myPid() + "";
		try {
			Process process = Runtime.getRuntime().exec(
					"logcat -d OFCONTROLLER:V *:S");
			BufferedReader bufferedReader = new BufferedReader(
					new InputStreamReader(process.getInputStream()));

			StringBuilder log = new StringBuilder();
			String line;
			while ((line = bufferedReader.readLine()) != null) {
				if (line.contains(pid)) {
					log.append(line.split(": ")[1] + "\n");
				}
			}
			
			tv.setText(log.toString());
		} catch (IOException e) {
		}
	}
}
