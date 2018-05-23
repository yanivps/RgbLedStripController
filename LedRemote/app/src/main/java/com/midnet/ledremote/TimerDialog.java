package com.midnet.ledremote;

import android.app.Activity;
import android.app.AlertDialog;
import android.app.Dialog;
import android.content.DialogInterface;
import android.os.Bundle;
import android.support.v4.app.DialogFragment;
import android.view.LayoutInflater;
import android.view.View;
import android.widget.Button;
import android.widget.NumberPicker;
import android.widget.RadioButton;
import android.widget.Toast;

public class TimerDialog extends DialogFragment {
    private int mHours;
    private int mMinutes;
    private int mSeconds;
    private TimerType mTimerType;

    public enum TimerType {
        TURN_ON,
        TURN_OFF
    }
    @Override
    public Dialog onCreateDialog(Bundle savedInstanceState) {
        AlertDialog.Builder builder = new AlertDialog.Builder(getActivity());
        // Get the layout inflater
        LayoutInflater inflater = getActivity().getLayoutInflater();

        // Inflate and set the layout for the dialog
        // Pass null as the parent view because its going in the dialog layout
        View dialogView = inflater.inflate(R.layout.timer_dialog, null);
        final NumberPicker npHours = dialogView.findViewById(R.id.hoursNumberPicker);
        final NumberPicker npMinutes = dialogView.findViewById(R.id.minutesNumberPicker);
        final NumberPicker npSeconds = dialogView.findViewById(R.id.secondsNumberPicker);

        npHours.setMaxValue(99);
        npHours.setMinValue(0);

        npMinutes.setMaxValue(59);
        npMinutes.setMinValue(0);

        npSeconds.setMaxValue(59);
        npSeconds.setMinValue(0);

        builder.setView(dialogView)
                // Add action buttons
                .setPositiveButton(R.string.set, new DialogInterface.OnClickListener() {
                    @Override
                    public void onClick(DialogInterface dialog, int id) {
                        //Do nothing here because we override this button later to change the close behaviour.
                        //However, we still need this because on older versions of Android unless we
                        //pass a handler the button doesn't get instantiated
                    }
                })
                .setNegativeButton(R.string.cancel, new DialogInterface.OnClickListener() {
                    public void onClick(DialogInterface dialog, int id) {
                        TimerDialog.this.getDialog().cancel();
                    }
                });

        return builder.create();
    }

    @Override
    public void onStart()
    {
        super.onStart();    //super.onStart() is where dialog.show() is actually called on the underlying dialog, so we have to do it after this point
        AlertDialog d = (AlertDialog)getDialog();
        if(d != null)
        {
            final NumberPicker npHours = d.findViewById(R.id.hoursNumberPicker);
            final NumberPicker npMinutes = d.findViewById(R.id.minutesNumberPicker);
            final NumberPicker npSeconds = d.findViewById(R.id.secondsNumberPicker);
            final RadioButton radioTurnOn = d.findViewById(R.id.turnOnRadio);
            final RadioButton radioTurnOff = d.findViewById(R.id.turnOffRadio);

            Button positiveButton = d.getButton(Dialog.BUTTON_POSITIVE);
            positiveButton.setOnClickListener(new View.OnClickListener()
            {
                @Override
                public void onClick(View v)
                {
                    mHours = npHours.getValue();
                    mMinutes = npMinutes.getValue();
                    mSeconds = npSeconds.getValue();
                    mTimerType = radioTurnOn.isChecked() ? TimerType.TURN_ON : TimerType.TURN_OFF;
                    
                    Boolean canCloseDialog = false;
                    if (radioTurnOn.isChecked() || radioTurnOff.isChecked()) {
                        canCloseDialog = true;
                    } else {
                        Toast.makeText(getActivity(), "You must select timer on or off", Toast.LENGTH_SHORT).show();
                    }
                    if (canCloseDialog) {
                        dismiss();
                        mListener.onDialogPositiveClick(TimerDialog.this);
                    }
                    //else dialog stays open. Make sure you have an obvious way to close the dialog especially if you set cancellable to false.
                }
            });
        }
    }


    /* The activity that creates an instance of this dialog fragment must
     * implement this interface in order to receive event callbacks.
     * Each method passes the DialogFragment in case the host needs to query it. */
    public interface TimerDialogListener {
        public void onDialogPositiveClick(DialogFragment dialog);
    }

    // Use this instance of the interface to deliver action events
    TimerDialogListener mListener;

    // Override the Fragment.onAttach() method to instantiate the TimerDialogListener
    @Override
    public void onAttach(Activity activity) {
        super.onAttach(activity);
        // Verify that the host activity implements the callback interface
        try {
            // Instantiate the TimerDialogListener so we can send events to the host
            mListener = (TimerDialogListener) activity;
        } catch (ClassCastException e) {
            // The activity doesn't implement the interface, throw exception
            throw new ClassCastException(activity.toString()
                    + " must implement TimerDialogListener");
        }
    }

    public int getHours() {
        return mHours;
    }

    public int getMinutes() {
        return mMinutes;
    }

    public int getSeconds() {
        return mSeconds;
    }

    public TimerType getTimerType() {
        return mTimerType;
    }
}
