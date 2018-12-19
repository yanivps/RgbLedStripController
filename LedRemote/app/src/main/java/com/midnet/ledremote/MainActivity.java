package com.midnet.ledremote;

import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.graphics.Color;
import android.net.nsd.NsdManager;
import android.net.nsd.NsdServiceInfo;
import android.os.AsyncTask;
import android.os.Bundle;
import android.preference.PreferenceManager;
import android.support.v4.app.DialogFragment;
import android.support.v7.app.AppCompatActivity;
import android.support.v7.widget.Toolbar;
import android.util.DisplayMetrics;
import android.util.Log;
import android.view.Menu;
import android.view.MenuItem;
import android.view.MotionEvent;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.SeekBar;
import android.widget.TextView;
import android.widget.Toast;

import java.io.IOException;
import java.io.OutputStream;
import java.net.InetAddress;
import java.net.Socket;
import java.net.UnknownHostException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;

public class MainActivity extends AppCompatActivity implements View.OnTouchListener, SeekBar.OnSeekBarChangeListener, TimerDialog.TimerDialogListener, AnimationDialog.AnimationDialogListener {
    public final static String TAG = "LedRemote";
    private static final int SETTINGS_ACTIVITY_REQUEST = 1;

    private static final byte START_MARKER = (byte) 0xFE;
    private static final byte END_MARKER = (byte) 0xFF;
    private static final byte SPECIAL_BYTE = (byte) 0xFD;
    private static final byte SET_COLOR_COMMAND = (byte) 0x01;
    private static final byte TIMER_ON_COMMAND = (byte) 0x02;
    private static final byte TIMER_OFF_COMMAND = (byte) 0x03;
    private static final byte ANIMATION_COMMAND = (byte) 0x04;
    private static final byte FADE_ANIMATION_CODE = (byte) 0x01;
    private static final byte BLINK_ANIMATION_CODE = (byte) 0x02;

    public ColorPickerView colorPicker;
    private SeekBar seek;
    private TextView headingText;
    private TextView initialText;
    private Button turnOffButton;
    private Button setTimerColorButton;
    private Button cancelTimerColorButton;
    private static final int blueStart = 100;

    private static final String SERVICE_TYPE = "_ledstrip._tcp.";
    private NsdManager.DiscoveryListener mDiscoveryListener;
    private NsdManager mNsdManager;
    private NsdManager.ResolveListener mResolveListener;

    private InetAddress mHostAddress;
    private int mPort;
    private Socket socket;
    private boolean IsAsyncRunning;
    private Thread checkConnectionThread;
    private SendDataToDeviceTask sendDataToDeviceTask;
    private boolean mIsConnected = false;
    private boolean mColorPickerAdded = false;
    private Object mCheckConnectionLockObject = new Object();
    private boolean mSettingTimer = false;
    private int mTimerOnColor;
    private int mCurrentColor;
    private int mCurrentSeekProgress;
    private int mTimerMilliseconds;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);
        Toolbar toolbar = findViewById(R.id.toolbar);
        setSupportActionBar(toolbar);

        final LinearLayout layout = findViewById(R.id.color_picker_layout);
        final int width = layout.getWidth();
        //get the display density
        DisplayMetrics metrics = new DisplayMetrics();
        getWindowManager().getDefaultDisplay().getMetrics(metrics);
        colorPicker = new ColorPickerView(this,blueStart,metrics.densityDpi);
        layout.setMinimumHeight(width);
        layout.setOnTouchListener(this);

        headingText = findViewById(R.id.heading_textview);
        headingText.setText(R.string.tap_a_color);

        initialText = findViewById(R.id.initial_textview);
        initialText.setText(R.string.connecting);
        initialText.setVisibility(View.VISIBLE);

        turnOffButton = findViewById(R.id.turnOffButton);
        setTimerColorButton = findViewById(R.id.setTimerOnColorButton);
        cancelTimerColorButton = findViewById(R.id.cancelTimerOnColorButton);

        seek = findViewById(R.id.seekBar1);
        seek.setProgress(blueStart);
        mCurrentSeekProgress = seek.getProgress();
        seek.setMax(255);
        seek.setOnSeekBarChangeListener(this);

        mNsdManager = (NsdManager) getSystemService(Context.NSD_SERVICE);
        initializeConnectionSettings();
        initializeDiscoveryListener();
        initializeResolveListener();

        AsyncTask.execute(new Runnable() {
            @Override
            public void run() {
                mNsdManager.discoverServices(
                        SERVICE_TYPE, NsdManager.PROTOCOL_DNS_SD, mDiscoveryListener);
                synchronized(mCheckConnectionLockObject) {
                    checkConnection();
                }
            }
        });
    }

    @Override
    public boolean onCreateOptionsMenu(Menu menu) {
        // Don't show menu buttons when choosing color for timer on
        if (mSettingTimer)
            return true;

        // Inflate the menu; this adds items to the action bar if it is present.
        getMenuInflater().inflate(R.menu.menu_main, menu);
        menu.findItem(R.id.action_set_timer).setVisible(mIsConnected);
        menu.findItem(R.id.action_animation).setVisible(mIsConnected);
        return true;
    }

    @Override
    public boolean onOptionsItemSelected(MenuItem item) {
        switch (item.getItemId()) {
            case R.id.action_settings:
                Intent i = new Intent(this, SettingsActivity.class);
                startActivityForResult(i, SETTINGS_ACTIVITY_REQUEST);
                return true;

            case R.id.action_set_timer:
                DialogFragment timerDialog = new TimerDialog();
                timerDialog.show(getSupportFragmentManager(), "timer");
                return true;

            case R.id.action_animation:
                DialogFragment animationDialog = new AnimationDialog();
                animationDialog.show(getSupportFragmentManager(), "animation");
                return true;

            default:
                // If we got here, the user's action was not recognized.
                // Invoke the superclass to handle it.
                return super.onOptionsItemSelected(item);
        }
    }

    @Override
    public void onAnimationDialogPositiveClick(DialogFragment dialog) {
        AnimationDialog animationDialog = (AnimationDialog) dialog;
        sendAnimationToLedDevice(
                animationDialog.getDuration(),
                animationDialog.getAnimationType(),
                animationDialog.getRandomColors());
    }

    @Override
    public void onTimerDialogPositiveClick(DialogFragment dialog) {
        TimerDialog timerDialog = (TimerDialog) dialog;
        mTimerMilliseconds = (timerDialog.getHours() * 60 + timerDialog.getMinutes() * 60 + timerDialog.getSeconds()) * 1000;
        TimerDialog.TimerType timerType = timerDialog.getTimerType();

        if (timerType == TimerDialog.TimerType.TURN_ON) {
            mTimerOnColor = mCurrentColor;
            mSettingTimer = true;
            turnOffButton.setVisibility(View.GONE);
            setTimerColorButton.setVisibility(View.VISIBLE);
            cancelTimerColorButton.setVisibility(View.VISIBLE);
            invalidateOptionsMenu();

            colorPicker.saveColorState();
        } else if (timerType == TimerDialog.TimerType.TURN_OFF) {
            sendTimerOffToLedDevice();
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        if (requestCode == SETTINGS_ACTIVITY_REQUEST) {
            reloadNewConnectionSettings();
        }
    }

    // sends color data to device as {START, CMD, R, G, B, END}
    private void sendColorToLedDevice(int color){
        Log.d(TAG, "Sending to led device color: " + color);
        byte[] data = {START_MARKER, SET_COLOR_COMMAND, (byte) Color.red(color),(byte)Color.green(color),(byte)Color.blue(color), END_MARKER};
        byte[] dataToSend = encodePacketData(data);

        if (!IsAsyncRunning) {
            sendDataToDeviceTask = new SendDataToDeviceTask();
            sendDataToDeviceTask.execute(new SendDataToDeviceTaskArgs(dataToSend));
            IsAsyncRunning = true;
        }
        else {
            if (sendDataToDeviceTask != null && sendDataToDeviceTask.getStatus() != AsyncTask.Status.FINISHED) {
                sendDataToDeviceTask.cancel(true);
            }
            sendDataToDeviceTask = new SendDataToDeviceTask();
            sendDataToDeviceTask.execute(new SendDataToDeviceTaskArgs(dataToSend));
            IsAsyncRunning = true;
        }
    }

    private byte[] encodePacketData(byte[] data) {
        int length = 2; // for start and end markers
        for (int i=1; i < data.length - 1; i++) { // don't count start and end markers
            length++;
            if (unsignedToBytes(data[i]) >= unsignedToBytes(SPECIAL_BYTE)) {
                length++;
            }
        }
        byte[] encodedData = new byte[length];
        encodedData[0] = data[0]; // copy start marker as is
        int j = 1; // skip start marker
        for (int i = 1; i < data.length - 1; i++){ // don't take start and end markers
            if (unsignedToBytes(data[i]) >= unsignedToBytes(SPECIAL_BYTE)) {
                //encode special characters
                encodedData[j] = SPECIAL_BYTE;
                j++;
                encodedData[j] = (byte) (unsignedToBytes(data[i]) - unsignedToBytes(SPECIAL_BYTE));
            } else {
                encodedData[j] = data[i];
            }
            j++;
        }
        encodedData[j] = data[data.length - 1]; // copy end marker as is
        return encodedData;
    }

    // sets the text boxes' text and color background.
    private void updateTextAreas(int col) {
        int[] colBits = {Color.red(col),Color.green(col),Color.blue(col)};
        //set the text & color backgrounds
        headingText.setText(String.format("You picked #%s%s%s", String.format("%02X", Color.red(col)), String.format("%02X", Color.green(col)), String.format("%02X", Color.blue(col))));
        headingText.setBackgroundColor(col);

        if (isDarkColor(colBits)) {
            headingText.setTextColor(Color.WHITE);
        } else {
            headingText.setTextColor(Color.BLACK);
        }
    }

    // returns true if the color is dark.  useful for picking a font color.
    public boolean isDarkColor(int[] color) {
        if (color[0]*.3 + color[1]*.59 + color[2]*.11 > 150) return false;
        return true;
    }

    @Override
    //called when the user touches the color palette
    public boolean onTouch(View view, MotionEvent event) {
        int color = 0;
        color = colorPicker.getColor(event.getX(),event.getY(),true);
        colorPicker.invalidate();
        //re-draw the selected colors text
        updateTextAreas(color);

        if (mSettingTimer) {
            mTimerOnColor = color;
        } else {
            mCurrentColor = color;
            sendColorToLedDevice(color);
        }
        return true;
    }

    @Override
    public void onProgressChanged(SeekBar seek, int progress, boolean fromUser) {
        int amt = seek.getProgress();
        int color = colorPicker.updateShade(amt);
        updateTextAreas(color);
        if (mSettingTimer) {
            mTimerOnColor = color;
        } else {
            mCurrentColor = color;
            mCurrentSeekProgress = amt;
            sendColorToLedDevice(color);
        }
        colorPicker.invalidate();
    }

    @Override
    public void onStartTrackingTouch(SeekBar arg0) {

    }

    @Override
    public void onStopTrackingTouch(SeekBar arg0) {

    }

    // generate a random hex color & display it
    public void randomColor(View v) {
        int z = (int) (Math.random()*255);
        int x = (int) (Math.random()*255);
        int y = (int) (Math.random()*255);
        colorPicker.setColor(x,y,z);
        SeekBar seek = findViewById(R.id.seekBar1);
        seek.setProgress(z);
    }

    // generate a random hex color & display it
    public void turnOffLed(View v) {
        headingText.setText(R.string.tap_a_color);
        headingText.setTextColor(Color.WHITE);
        headingText.setBackgroundColor(0);
        colorPicker.setColor(0, 0, 0);
        colorPicker.noColor();
        colorPicker.invalidate();
        mCurrentColor = 0;
        sendColorToLedDevice(0);
    }

    public void setTimerOnColor(View v) {
        sendTimerOnToLedDevice();
        turnOffButton.setVisibility(View.VISIBLE);
        setTimerColorButton.setVisibility(View.GONE);
        cancelTimerColorButton.setVisibility(View.GONE);
        restoreColorState();
        mSettingTimer = false;
        invalidateOptionsMenu();
    }

    public void cancelTimerOnColor(View v) {
        turnOffButton.setVisibility(View.VISIBLE);
        setTimerColorButton.setVisibility(View.GONE);
        cancelTimerColorButton.setVisibility(View.GONE);
        restoreColorState();
        mSettingTimer = false;
        invalidateOptionsMenu();
    }

    private void restoreColorState() {
        if (mCurrentColor != 0) {
            seek.setProgress(mCurrentSeekProgress);
            colorPicker.restoreColorState();
            updateTextAreas(mCurrentColor);
        } else {
            headingText.setText(R.string.tap_a_color);
            headingText.setTextColor(Color.WHITE);
            headingText.setBackgroundColor(0);
            colorPicker.setColor(0, 0, 0);
            colorPicker.noColor();
            colorPicker.invalidate();
        }
    }

    public void sendTimerOnToLedDevice() {
        Log.d(TAG, "Sending to led device color: " + mTimerOnColor);
        byte[] serializedTimer = ByteBuffer.allocate(4).order(ByteOrder.LITTLE_ENDIAN).putInt(mTimerMilliseconds).array();
        byte[] data = new byte[10];
        data[0] = START_MARKER;
        data[1] = TIMER_ON_COMMAND;
        data[2] = (byte) Color.red(mTimerOnColor);
        data[3] = (byte) Color.green(mTimerOnColor);
        data[4] = (byte) Color.blue(mTimerOnColor);
        for (int i = 0; i < 4; i++) {
            data[i + 5] = serializedTimer[i];
        }
        data[9] = END_MARKER;
        byte[] dataToSend = encodePacketData(data);
        sendDataToDeviceTask = new SendDataToDeviceTask();
        sendDataToDeviceTask.execute(new SendDataToDeviceTaskArgs(dataToSend, "On timer set!"));
    }

    public void sendTimerOffToLedDevice() {
        Log.d(TAG, "Sending to led device timer off");
        byte[] serializedTimer = ByteBuffer.allocate(4).order(ByteOrder.LITTLE_ENDIAN).putInt(mTimerMilliseconds).array();
        byte[] data = new byte[7];
        data[0] = START_MARKER;
        data[1] = TIMER_OFF_COMMAND;
        for (int i = 0; i < 4; i++) {
            data[i + 2] = serializedTimer[i];
        }
        data[6] = END_MARKER;
        byte[] dataToSend = encodePacketData(data);
        sendDataToDeviceTask = new SendDataToDeviceTask();
        sendDataToDeviceTask.execute(new SendDataToDeviceTaskArgs(dataToSend, "Off timer set!"));
    }

    public void sendAnimationToLedDevice(float duration, AnimationDialog.AnimationType animationType, Boolean randomColors) {
        short durationInMillis = (short) (duration * 1000);
        byte animationCode;
        switch (animationType) {
            case FADE:
                animationCode = FADE_ANIMATION_CODE;
                break;
            case BLINK:
                animationCode = BLINK_ANIMATION_CODE;
                break;
            default:
                animationCode = 0; // Stops animation
                break;
        }
        Log.d(TAG, "Sending to led device animation");
        byte[] serializedDuration = ByteBuffer.allocate(2).order(ByteOrder.LITTLE_ENDIAN).putShort(durationInMillis).array();
        byte[] data = new byte[7];
        data[0] = START_MARKER;
        data[1] = ANIMATION_COMMAND;
        data[2] = animationCode;
        for (int i = 0; i < 2; i++) {
            data[i + 3] = serializedDuration[i];
        }
        data[5] = (byte) (randomColors ? 0x01 : 0x00);
        data[6] = END_MARKER;
        byte[] dataToSend = encodePacketData(data);
        sendDataToDeviceTask = new SendDataToDeviceTask();
        String message;
        if (animationCode == 0 || durationInMillis == 0) {
            message = "Stops animation!";
        } else {
            String animationName = animationType.name().toLowerCase();
            animationName = animationName.substring(0, 1).toUpperCase() + animationName.substring(1);
            message = String.format("%s animation of %.2f sec set!", animationName, duration);
        }
        sendDataToDeviceTask.execute(new SendDataToDeviceTaskArgs(dataToSend, message));
    }

    public void checkConnection() {
        if (mIsConnected) return;
        try {
            // Checking connection
            socket = new Socket(mHostAddress, mPort);
            OutputStream out = socket.getOutputStream();
            out.write(new byte[]{END_MARKER}); // Send endMarker to release server from waiting on readBytesUntil
            out.flush();
            out.close();
            socket.close();
            if (Thread.interrupted()) return;
            mIsConnected = true;
            runOnUiThread(new Runnable() {
                @Override
                public void run() {
                    if (!mColorPickerAdded) {
                        LinearLayout layout = findViewById(R.id.color_picker_layout);
                        layout.addView(colorPicker);
                        mColorPickerAdded = true;
                    }
                    initialText.setVisibility(View.GONE);
                    Toast.makeText(MainActivity.this, "Connected!", Toast.LENGTH_SHORT).show();
                    invalidateOptionsMenu();
                }
            });
        } catch (IOException e) {
            if (Thread.interrupted()) return;
            Log.d(TAG, getString(R.string.could_not_connect));
            runOnUiThread(new Runnable() {
                @Override
                public void run() {
                    initialText.setText(R.string.could_not_connect);
                    initialText.setVisibility(View.VISIBLE);
                }
            });
        }
    }

    private void reloadNewConnectionSettings() {
        InetAddress oldHostAddress = mHostAddress;
        int oldPort = mPort;
        initializeConnectionSettings();
        if (!oldHostAddress.equals(mHostAddress) || oldPort != mPort) {
            // Connection settings were changed. check connection again
            mIsConnected = false;
            invalidateOptionsMenu();
            initialText.setText(R.string.connecting);
            initialText.setVisibility(View.VISIBLE);
            if (checkConnectionThread != null) {
                // If settings were changed, stop last try to connect before starting a new one
                checkConnectionThread.interrupt();
            }
            checkConnectionThread = new Thread(new CheckConnectionTask());
            checkConnectionThread.start();
        }
    }

    public class CheckConnectionTask implements Runnable {
        @Override
        public void run() {
            checkConnection();
        }
    }

    public class SendDataToDeviceTask extends AsyncTask<SendDataToDeviceTaskArgs, Void, Void>
    {
        @Override
        protected Void doInBackground(final SendDataToDeviceTaskArgs... args)
        {
            try {
                socket = new Socket(mHostAddress, mPort);
                OutputStream out = socket.getOutputStream();
                out.write(args[0].dataToSend);
                out.flush();
                out.close();
                if (args[0].onSentMessage != null) {
                    runOnUiThread(new Runnable() {
                        @Override
                        public void run() {
                            Toast.makeText(MainActivity.this, args[0].onSentMessage, Toast.LENGTH_SHORT).show();
                        }
                    });
                }
            } catch (IOException e) {
                Log.d(TAG, "Could not send data to remote device.");
                // Probably wifi turned off. Try get connection details from settings again
                initializeConnectionSettings();
            }
            return null;
        }

        @Override
        protected void onPostExecute(Void aVoid)
        {
            IsAsyncRunning = false;
        }
    }

    private void initializeConnectionSettings() {
        SharedPreferences sharedPref = PreferenceManager.getDefaultSharedPreferences(this);
        String ipString = sharedPref.getString(SettingsActivity.KEY_PREF_IP_ADDRESS, "");
        String port = sharedPref.getString(SettingsActivity.KEY_PREF_PORT, "4000");

        try {
            mHostAddress = InetAddress.getByName(ipString);
        } catch (UnknownHostException e) {
            Toast.makeText(MainActivity.this, "Invalid IP Address in settings", Toast.LENGTH_LONG).show();
            try { mHostAddress = InetAddress.getByName(""); } catch (UnknownHostException e1) {}
        }
        mPort = Integer.valueOf(port);
    }

    public void initializeDiscoveryListener() {

        // Instantiate a new DiscoveryListener
        mDiscoveryListener = new NsdManager.DiscoveryListener() {

            // Called as soon as service discovery begins.
            @Override
            public void onDiscoveryStarted(String regType) {
                Log.d(TAG, "Service discovery started");
            }

            @Override
            public void onServiceFound(NsdServiceInfo service) {
                // A service was found! Do something with it.
                Log.d(TAG, "Service discovery success" + service);
                if (!service.getServiceType().equals(SERVICE_TYPE)) {
                    // Service type is the string containing the protocol and
                    // transport layer for this service.
                    Log.d(TAG, "Unknown Service Type: " + service.getServiceType());
                } else {
                    mNsdManager.resolveService(service, mResolveListener);
                }
            }

            @Override
            public void onServiceLost(NsdServiceInfo service) {
                // When the network service is no longer available.
                // Internal bookkeeping code goes here.
                Log.e(TAG, "service lost" + service);
            }

            @Override
            public void onDiscoveryStopped(String serviceType) {
                Log.i(TAG, "Discovery stopped: " + serviceType);
            }

            @Override
            public void onStartDiscoveryFailed(String serviceType, int errorCode) {
                Log.e(TAG, "Discovery failed: Error code:" + errorCode);
                mNsdManager.stopServiceDiscovery(this);
            }

            @Override
            public void onStopDiscoveryFailed(String serviceType, int errorCode) {
                Log.e(TAG, "Discovery failed: Error code:" + errorCode);
                mNsdManager.stopServiceDiscovery(this);
            }
        };
    }

    public void initializeResolveListener() {
        mResolveListener = new NsdManager.ResolveListener() {

            @Override
            public void onResolveFailed(NsdServiceInfo serviceInfo, int errorCode) {
                // Called when the resolve fails. Use the error code to debug.
                Log.e(TAG, "Resolve failed" + errorCode);
            }

            @Override
            public void onServiceResolved(NsdServiceInfo serviceInfo) {
                Log.e(TAG, "Resolve Succeeded. " + serviceInfo);

                mHostAddress = serviceInfo.getHost();
                mPort = serviceInfo.getPort();
                synchronized(mCheckConnectionLockObject) {
                    checkConnection();
                }
            }
        };
    }

    public static int unsignedToBytes(byte b) {
        return b & 0xFF;
    }

    public class SendDataToDeviceTaskArgs {
        byte[] dataToSend;
        public String onSentMessage;

        SendDataToDeviceTaskArgs(byte[] dataToSend) {
            this(dataToSend, null);
        }
        SendDataToDeviceTaskArgs(byte[] dataToSend, String onSentMessage) {
            this.dataToSend = dataToSend;
            this.onSentMessage = onSentMessage;
        }
    }
}
