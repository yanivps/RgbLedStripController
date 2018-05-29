package com.midnet.ledremote;

import android.app.Activity;
import android.app.AlertDialog;
import android.app.Dialog;
import android.content.DialogInterface;
import android.os.Bundle;
import android.support.v4.app.DialogFragment;
import android.view.LayoutInflater;
import android.view.View;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.ListAdapter;
import android.widget.NumberPicker;
import android.widget.Spinner;
import android.widget.Toast;

public class AnimationDialog extends DialogFragment {
    private float mDuration;
    private AnimationType mAnimationType;
    private Boolean mRandomColors = false;

    public enum AnimationType {
        OFF,
        FADE,
        BLINK
    }
    @Override
    public Dialog onCreateDialog(Bundle savedInstanceState) {
        AlertDialog.Builder builder = new AlertDialog.Builder(getActivity());
        // Get the layout inflater
        LayoutInflater inflater = getActivity().getLayoutInflater();

        // Inflate and set the layout for the dialog
        // Pass null as the parent view because its going in the dialog layout
        View dialogView = inflater.inflate(R.layout.animation_dialog, null);
        Spinner staticSpinner = dialogView.findViewById(R.id.animationSpinner);

        // Create an ArrayAdapter using the string array and a default spinner
        ArrayAdapter<CharSequence> animationAdapter = ArrayAdapter
                .createFromResource(getActivity(), R.array.animation_array,
                        android.R.layout.simple_spinner_item);

        // Specify the layout to use when the list of choices appears
        animationAdapter
                .setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);

        // Apply the adapter to the spinner
        staticSpinner.setAdapter(animationAdapter);


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
                .setNeutralButton(R.string.stop, new DialogInterface.OnClickListener() {
                    @Override
                    public void onClick(DialogInterface dialog, int which) {
                        mDuration = 0;
                        mAnimationType = AnimationType.OFF;
                        mListener.onAnimationDialogPositiveClick(AnimationDialog.this);
                    }
                })
                .setNegativeButton(R.string.cancel, new DialogInterface.OnClickListener() {
                    public void onClick(DialogInterface dialog, int id) {
                        AnimationDialog.this.getDialog().cancel();
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
            final Spinner spinnerAnimation = d.findViewById(R.id.animationSpinner);
            final EditText etDuration = d.findViewById(R.id.animationDurationEditText);
            final CheckBox cbRandomColors = d.findViewById(R.id.randomColorsCheckBox);

            Button positiveButton = d.getButton(Dialog.BUTTON_POSITIVE);
            positiveButton.setOnClickListener(new View.OnClickListener()
            {
                @Override
                public void onClick(View v)
                {
                    String durationText = etDuration.getText().toString();
                    try {
                        mDuration = Float.parseFloat(durationText);
                    } catch (NumberFormatException ex) {
                        mDuration = 0;
                    }
                    int position = spinnerAnimation.getSelectedItemPosition();
                    try {
                        mAnimationType = AnimationType.values()[position];
                    } catch (IndexOutOfBoundsException ex) {
                        mAnimationType = AnimationType.OFF;
                    }
                    mRandomColors = cbRandomColors.isChecked();

                    Boolean canCloseDialog = true;
                    if (position == 0) {
                        Toast.makeText(getActivity(), "Animation is missing", Toast.LENGTH_SHORT).show();
                        canCloseDialog = false;
                    } else if (durationText.equals("")) {
                        Toast.makeText(getActivity(), "Animation duration is missing", Toast.LENGTH_SHORT).show();
                        canCloseDialog = false;
                    }

                    if (canCloseDialog) {
                        dismiss();
                        mListener.onAnimationDialogPositiveClick(AnimationDialog.this);
                    }
                    //else dialog stays open. Make sure you have an obvious way to close the dialog especially if you set cancellable to false.
                }
            });
        }
    }


    /* The activity that creates an instance of this dialog fragment must
     * implement this interface in order to receive event callbacks.
     * Each method passes the DialogFragment in case the host needs to query it. */
    public interface AnimationDialogListener {
        public void onAnimationDialogPositiveClick(DialogFragment dialog);
    }

    // Use this instance of the interface to deliver action events
    AnimationDialogListener mListener;

    // Override the Fragment.onAttach() method to instantiate the AnimationDialogListener
    @Override
    public void onAttach(Activity activity) {
        super.onAttach(activity);
        // Verify that the host activity implements the callback interface
        try {
            // Instantiate the AnimationDialogListener so we can send events to the host
            mListener = (AnimationDialogListener) activity;
        } catch (ClassCastException e) {
            // The activity doesn't implement the interface, throw exception
            throw new ClassCastException(activity.toString()
                    + " must implement TimerDialogListener");
        }
    }

    public float getDuration() {
        return mDuration;
    }

    public AnimationType getAnimationType() {
        return mAnimationType;
    }

    public Boolean getRandomColors() {
        return mRandomColors;
    }
}
